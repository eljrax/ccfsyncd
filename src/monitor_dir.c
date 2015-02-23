#include "ccfsync.h"
#include <sys/types.h>

#include <sys/inotify.h>

#define EVENT_SIZE  ( sizeof (struct inotify_event) )
#define BUF_LEN     ( 1024 * ( EVENT_SIZE + 16 ) )

gboolean
event_wd_to_dir (gpointer key, gpointer value, gpointer user_data)
{
  return (GPOINTER_TO_INT (value)) == GPOINTER_TO_INT (user_data);
}

gchar *
wd_to_dir (GHashTable * watches, int wd)
{
  GHashTableIter iter;
  gpointer key, value;
  pthread_mutex_lock (&watches_mutex);
  g_hash_table_iter_init (&iter, watches);
  while (g_hash_table_iter_next (&iter, &key, &value)) {
    if (GPOINTER_TO_INT (value) == wd) {
      pthread_mutex_unlock (&watches_mutex);
      return key;
    }
  }
  pthread_mutex_unlock (&watches_mutex);
  return NULL;

}



int
add_watches_recursively (char *dir, int inotify_fd, GHashTable ** watches, int monitor_events)
{

  unsigned int i;
  int wd;
  GList *dirs = get_dirs (dir, dir);
  dirs = g_list_prepend (dirs, g_strdup (dir));
  for (i = 0; i < g_list_length (dirs); i++) {
/* TODO: inotify_add_watch doesn't warn if the directory doesn't exist.. check before - also check return code for -1s */
    gchar *tmp_dir = g_strdup ((gchar *) g_list_nth_data (dirs, i));
    wd = inotify_add_watch (inotify_fd, tmp_dir, monitor_events);
    if (wd < 0) {
      return wd;
    }
    pthread_mutex_lock (&watches_mutex);
    g_hash_table_insert (*watches, tmp_dir, GINT_TO_POINTER (wd));
    pthread_mutex_unlock (&watches_mutex);
  }
  g_list_free_full (dirs, free_single_pointer);
  return 0;
}

void *
monitor_dir_inotify (void *data)
{

  struct exclusions *exclusions = data;
  GHashTable *watches = g_hash_table_new_full (g_str_hash, g_str_equal,
					       (GDestroyNotify) free_single_pointer, NULL);
//  GList *move_events = NULL;
  int length, i = 0;
  unsigned int j = 0;
  int fd, wd;
  char buffer[BUF_LEN];
  int monitor_events = IN_CREATE | IN_DELETE | IN_MODIFY | IN_MOVE;
  fd = inotify_init ();

  if (fd < 0)
    suicide ("Failed to initialise inotify: %s", strerror (errno));


  /* Get all the existing dirs and monitor them */

  if ((add_watches_recursively (cfg->monitor_dir, fd, &watches, monitor_events)) < 0) {
    suicide ("Error recursively adding inotify watches: %s", strerror (errno));
  }

  while (1) {
    i = 0;
    length = read (fd, buffer, BUF_LEN);

    if (length < 0) {
      g_hash_table_destroy (watches);
      perror ("read");
      pthread_exit (NULL);
    }

    while (i < length) {
      struct inotify_event *event = (struct inotify_event *) &buffer[i];
      if (event->len) {

	/* Translate everything into its full path */
	gchar *event_dir = wd_to_dir (watches, event->wd);
	if (event_dir == NULL) {
	  suicide ("event_dir is NULL for name %s and wd %d\n", event->name, event->wd);
	  continue;
	}

	/* Build a full path from / based on watch descriptor and event->name */
	gchar *tmp_path = NULL;
	Sasprintf (tmp_path, "%s/%s", (char *) event_dir, event->name);
	if (regex_match (tmp_path, exclusions)) {
	  log_msg (LOG_DEBUG, "Ignoring event on %s due to explicit exclusion", tmp_path);
	  free_single_pointer (tmp_path);
	  i += EVENT_SIZE + event->len;
	  continue;
	}
	log_msg (LOG_DEBUG, "event_dir = %s, file: %s", event_dir, event->name);

	/* This represents the relative path of the event from the dir being monitored (as is on cloud files) */
	gchar *cf_tmp_path = g_strdup (tmp_path + strlen (cfg->monitor_dir) + 1);

	if (event->mask & IN_CREATE) {
	  if (event->mask & IN_ISDIR) {
	    /* Because there's a race condition in inotify, where files can be created before we have had time to 
	     * add the inotify watch, we need to scan any created directory, just in case it was cp -rf:ed or similar
	     */
	    struct move_thread_data *mtd = malloc (sizeof (struct move_thread_data));
	    mtd->fd = fd;
	    mtd->watches = watches;
	    mtd->tmp_path = g_strdup (tmp_path);
	    mtd->cf_tmp_path = g_strdup (cf_tmp_path);
	    mtd->events_mask = monitor_events;
	    mtd->exclusions = exclusions;
	    mtd->ev = NULL;	//malloc(sizeof(struct inotify_event));

	    pthread_t handle_dir_create_thread;
	    pthread_attr_t attr;
	    pthread_attr_init (&attr);
	    pthread_attr_setdetachstate (&attr, PTHREAD_CREATE_DETACHED);
	    int pthread_ret = pthread_create (&handle_dir_create_thread, &attr, handle_dir_create, mtd);
	    if (pthread_ret != 0) {
	      suicide ("Failed to create thread for handling directory move: %s\n", strerror (errno));
	    }
	  }
	  else {
	    local_file *lf = stat_local_file (g_strdup (tmp_path), cfg->monitor_dir);
	    /* There's a potential race here, where the file might be deleted nearly immediately after being created - ignore this case */
	    if (lf != NULL) {

	      pthread_mutex_lock (&files_being_uploaded_mutex);

	      files_being_uploaded = g_list_prepend (files_being_uploaded, g_strdup(lf->name));
	      g_async_queue_push (files_to_upload, lf);

	      pthread_mutex_unlock (&files_being_uploaded_mutex);
	    }
	  }
	}




	/* Directory move */
	/* Let me take this opportunity to express my most sincere, heartfelt hatred for all things recursion... */
	else if (event->mask & IN_MOVE) {

	  log_msg (LOG_DEBUG, "%s move event on %s", event->mask & IN_ISDIR ? "Directory" : "File", tmp_path);
	  if (event->mask & IN_ISDIR) {
	    if (event->mask & IN_MOVED_FROM) {
	      struct move_event *me = malloc (sizeof (struct move_event));

	      me->cookie = event->cookie;
	      me->event_name = g_strdup (event->name);
	      me->cf_name = g_strdup (cf_tmp_path);
	      me->full_local_path = g_strdup (tmp_path);

	      pthread_mutex_lock (&move_events_mutex);
	      move_events = g_list_prepend (move_events, me);
	      pthread_mutex_unlock (&move_events_mutex);

	    }

	    //  This has the potential of taking a bit of time - esp. the call to get_cf_files_from_dir - so this needs to be in its own thread.
	    // otherwise we may lose events! Put the move_events on a queue, have another thread do the other stuff!
	    else if (event->mask & IN_MOVED_TO) {

	      struct move_thread_data *mtd = malloc (sizeof (struct move_thread_data));

	      mtd->fd = fd;
	      mtd->watches = watches;
	      mtd->events_mask = 0;
	      mtd->tmp_path = g_strdup (tmp_path);
	      mtd->cf_tmp_path = g_strdup (cf_tmp_path);
	      mtd->ev = malloc (sizeof (struct inotify_event));

	      memcpy (mtd->ev, event, sizeof (struct inotify_event));

	      pthread_t handle_dir_move_thread;
	      pthread_attr_t attr;
	      pthread_attr_init (&attr);
	      pthread_attr_setdetachstate (&attr, PTHREAD_CREATE_DETACHED);


	      int pthread_ret = pthread_create (&handle_dir_move_thread, &attr, handle_dir_move, mtd);
	      if (pthread_ret != 0) {
		suicide ("Failed to create thread for handling directory move: %s\n", strerror (errno));
	      }
	    }
	  }

	  /* File move */
	  else {
	    if (event->mask & IN_MOVED_TO) {
	      /* Here we need to find the corresponding event cookie, put the file on the files_to_copy queue
	       * and then delete the file in the MOVED_FROM event found in the list
	       */

	      pthread_mutex_lock (&move_events_mutex);

	      for (j = 0; j < g_list_length (move_events); j++) {

		struct move_event *me = g_list_nth_data (move_events, j);

		if (me->cookie == event->cookie) {
		  cf_file_copy *cfc = malloc (sizeof (cf_file_copy));
		  cfc->old_name = g_strdup (me->cf_name);
		  cfc->new_name = g_strdup (cf_tmp_path);
		  cfc->sentinel = g_strdup ("ok");
		  cfc->cf_file = build_cf_file_from_lf (me->cf_name);

		  g_async_queue_push (files_to_copy, cfc);

		  move_events = g_list_remove (move_events, me);
		  log_msg (LOG_DEBUG, "File moved to: %s from: %s", tmp_path, cfc->old_name);
		}

		free_single_pointer (me->cf_name);
		free_single_pointer (me->event_name);
		free_single_pointer (me->full_local_path);
		free_single_pointer (me);
	      }

	      pthread_mutex_unlock (&move_events_mutex);

	    }
	    else if (event->mask & IN_MOVED_FROM) {
	      /* Put the event of the original file on a list, we need to look for 
	       * the corresponding cookie in the IN_MOVED_TO event
	       */
	      struct move_event *me = malloc (sizeof (struct move_event));

	      me->cookie = event->cookie;
	      me->cf_name = g_strdup (cf_tmp_path);
	      me->event_name = g_strdup (event->name);
	      me->full_local_path = g_strdup (tmp_path);

	      pthread_mutex_lock (&move_events_mutex);
	      move_events = g_list_prepend (move_events, me);
	      pthread_mutex_unlock (&move_events_mutex);

	    }
	  }
	}			// event->mask & IN_MOVE



	else if (event->mask & IN_DELETE) {

	  log_msg (LOG_DEBUG, "%s delete event on %s", event->mask & IN_ISDIR ? "Directory" : "File", tmp_path);

	  if (event->mask & IN_ISDIR) {
	    /* This is a no-op for us as recursive deletion also deletes files within the directory (which in turn generates separate 
	     * inotify events. Just need to remove the watcher. 
	     */
	    pthread_mutex_lock (&watches_mutex);

	    gpointer *tmp_wd = g_hash_table_lookup (watches, tmp_path);
	    inotify_rm_watch (fd, GPOINTER_TO_INT (tmp_wd));

	    pthread_mutex_unlock (&watches_mutex);
	  }
	  else {
	    cf_file *cf = build_cf_file_from_lf (cf_tmp_path);
	    g_async_queue_push (files_to_delete, cf);
	  }
	}



	else if (event->mask & IN_MODIFY) {

	  log_msg (LOG_DEBUG, "%s modify event on %s", event->mask & IN_ISDIR ? "Directory" : "File", tmp_path);

	  /* A directory mofification is a NOOP for us - only care about files */
	  if (!(event->mask & IN_ISDIR)) {
	    /* We can get more than one of these per file change as far as the person at the keyboard is concerned,
	     * so we need to prevent uploading the same file twice
	     */
	    if (event->mask |= IN_CLOSE_WRITE) {

	      gchar *f_name = g_strdup (tmp_path);
	      local_file *lf = stat_local_file (f_name, cfg->monitor_dir);

	      if (lf != NULL) {

		pthread_mutex_lock (&files_being_uploaded_mutex);

                if (g_list_find_custom(files_being_uploaded, lf->name, (GCompareFunc) g_ascii_strcasecmp)){
		  log_msg (LOG_DEBUG, "File '%s' is already being uploaded - ignoring event\n", lf->name);
		  destroy_local_file (lf);
                }
		else {
		  files_being_uploaded = g_list_prepend (files_being_uploaded, g_strdup(lf->name));
		  g_async_queue_push (files_to_upload, lf);
		}

		pthread_mutex_unlock (&files_being_uploaded_mutex);
		log_msg (LOG_DEBUG, "IN_CLOSE_WRITE The file %s was modified.\n", event->name);
	      }
	    }

	  }
	}


	free_single_pointer (cf_tmp_path);
	free_single_pointer (tmp_path);
      }
      i += EVENT_SIZE + event->len;
    }
  }

  (void) inotify_rm_watch (fd, wd);
  (void) close (fd);

  return 0;



}
