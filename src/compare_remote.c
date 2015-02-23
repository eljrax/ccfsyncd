#include "ccfsync.h"

/* Returns a list of files we have locally, but not on remote. Upload any local files not found, or different hash on CF */
GList *
compare_remote (GHashTable * local, GHashTable * remote)
{
  log_msg (LOG_DEBUG, "Entered compare_remote");
  GHashTableIter iter;
  /* We need to free the files we don't need, but can't do it yet since we need
   * them for comparing in compare_local. So delay purging of files we don't need to upload
   */
  GList *to_be_free = NULL;
  gpointer key, itr_value, cf_file_ptr;
  g_hash_table_iter_init (&iter, local);
  while (g_hash_table_iter_next (&iter, &key, &itr_value)) {
    local_file *lf = (local_file *) itr_value;
    /* Check file names */
    if (g_hash_table_lookup_extended (remote, lf->cf_name, NULL, &cf_file_ptr)) {

      /* Check hashes in case a file has been altered */
      cf_file *cf = (cf_file *) cf_file_ptr;
      if (strncmp (cf->hash, lf->hash, strlen (cf->hash)) != 0) {
	log_msg (LOG_DEBUG, "Hash mismatch between local '%s' and remote '%s' - need re-uploading!", lf->name, cf->local_path);
	g_async_queue_push (files_to_upload, lf);
      }
      else {
	/* We no longer need this file - files needing uploaded are free'd when uploaded */
	to_be_free = g_list_prepend (to_be_free, g_strdup (key));
      }
    }
    else {
      log_msg (LOG_DEBUG, "NOT found in remote: '%s' - need uploading", (char *) key);
      g_async_queue_push (files_to_upload, lf);
      continue;
    }

  }

  return to_be_free;

}

/* Puts files we don't have locally, but we have on CF onto the delete queue */
void
compare_local (GHashTable * remote, GHashTable * local)
{

  GHashTableIter iter;
  gpointer key, itr_value;
  g_hash_table_iter_init (&iter, remote);
  while (g_hash_table_iter_next (&iter, &key, &itr_value)) {
    cf_file *cf = itr_value;
    if (g_hash_table_lookup_extended (local, cf->local_path, NULL, NULL)) {
      destroy_cf_file (cf, cf->name);
      continue;
    }
    else {
      log_msg (LOG_DEBUG, "Found file on remote NOT found in local: '%s' - deleting", (char *) key);
      g_async_queue_push (files_to_delete, cf);
    }

  }
}
