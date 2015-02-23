#include "ccfsync.h"
#include <pthread.h>
#include <openssl/crypto.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

int
do_upload (gchar * token_header, gchar * cf_url, gchar * file, curl_off_t file_len, int thid)
{

  FILE *src = fopen (file, "rb");
  if (src == NULL) {
    log_msg (LOG_WARNING, "Upload thread %d: Failed to open file for reading in upload_file(): %s\n", thid, strerror (errno));
    return -1;
  }

  CURL *curl;
  CURLcode res;
  long http_code;
  struct curl_slist *headerlist = NULL;

  if ((curl = curl_easy_init ()) == NULL) {
    log_msg (LOG_ERR, "Upload thread %d: Failed to initialise curl!", thid);
    return -1;
  }

  headerlist = curl_slist_append (headerlist, token_header);
  curl_easy_setopt (curl, CURLOPT_HTTPHEADER, headerlist);
  curl_easy_setopt (curl, CURLOPT_UPLOAD, 1L);
  curl_easy_setopt (curl, CURLOPT_PUT, 1L);
  curl_easy_setopt (curl, CURLOPT_URL, cf_url);
  curl_easy_setopt (curl, CURLOPT_READDATA, src);
  if (!cfg->debug)
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_devnull);
  curl_easy_setopt (curl, CURLOPT_INFILESIZE_LARGE, file_len);

  res = curl_easy_perform (curl);
  curl_easy_getinfo (curl, CURLINFO_RESPONSE_CODE, &http_code);
  if (res != CURLE_OK)
    log_msg (LOG_ERR, "Upload thread %d: Request failed: %s\n", thid, curl_easy_strerror (res));

  fclose (src);
  curl_slist_free_all (headerlist);
  curl_easy_cleanup (curl);

  /* This is to work around a memory-leak in curl */
  ERR_remove_thread_state (NULL);

  return http_code;
}

/* Blocks on popping the queue containing files to upload 
 * takes a struct upload_thread_data containing the auth struct and container name
*/
void *
upload_file (void *data)
{

  thread_data *thd = (thread_data *) data;

  log_msg (LOG_DEBUG, "Upload thread: %d spawned", thd->thread_id);

  while (1) {

    int retries = 5;
    int http_code = 0;
    unsigned int i;
    int was_uploaded = FALSE;

    local_file *lf = g_async_queue_pop (files_to_upload);

    /* A sentinel-event is put on the queue when we're asked to exit. Kill the thread when we hit one of those */
    if (lf->sentinel != NULL) {
      if (strncmp (lf->sentinel, "exit", strlen ("exit")) == 0) {
	destroy_local_file (lf);
	log_msg (LOG_DEBUG, "Upload thread %d: asked to exit...\n", thd->thread_id);
	free_single_pointer (thd);
	pthread_exit (EXIT_SUCCESS);
      }
    }

    gchar *cf_url = NULL;
    gchar *token_header = NULL;


    do {
      Sasprintf (cf_url, "%s/%s/%s", auth->endpoint, cfg->container, lf->cf_name);

      log_msg (LOG_DEBUG, "\n\nUpload thread %d: Uploading '%s'", thd->thread_id, lf->name);
      log_msg (LOG_DEBUG, "Upload thread %d: Sending auth header: %s", thd->thread_id, auth->token_header);
      log_msg (LOG_DEBUG, "Upload thread %d: Using url: %s", thd->thread_id, cf_url);
      log_msg (LOG_DEBUG, "Upload thread %d: CF file is: %s", thd->thread_id, lf->cf_name);

      http_code = do_upload (auth->token_header, cf_url, lf->name, (curl_off_t) lf->st->st_size, thd->thread_id);
      log_msg (LOG_DEBUG, "Upload thread %d: HTTP return code: %d", thd->thread_id, http_code);
      
      if (http_code == 201) {
	was_uploaded = TRUE;
        free_single_pointer (cf_url);
	break;
      }
      else if (http_code == 401) {
	log_msg (LOG_INFO, "Upload thread %d: Authentication error - token expired? Reauthenticating\n", thd->thread_id);

	if (pthread_mutex_trylock (&auth_in_progress_mutex)) {
	  doAuth (REAUTH);
	  pthread_mutex_unlock (&auth_in_progress_mutex);
	  log_msg (LOG_DEBUG, "Upload thread %d: Got new token: '%s'", thd->thread_id, auth->token);
	}
	else {
	  log_msg (LOG_DEBUG, "Upload thread %d: Another thread is authenticating - sleeping 1 second and re-trying upload", thd->thread_id);
	}
      }
      else {
	log_msg (LOG_DEBUG, "Upload thread %d: Unhandled HTTP return code in file upload: %d file: %s", thd->thread_id, http_code, lf->name);
      }

      free_single_pointer (cf_url);
      cf_url = NULL;
      sleep (1);

    } while (retries-- > 0);

    if (!was_uploaded) {
      log_msg (LOG_ERR, "Upload thread: %d: WARNING: File '%s' failed to upload! HTTP return code: %d", thd->thread_id, lf->name, http_code);
    }
    else
      log_msg (LOG_DEBUG, "Upload thread: %d: Upload of '%s' successful", thd->thread_id, lf->name);

    /* Remove the file from the list holding files waiting to be uploaded.
     * This will enable the file to be uploaded again from this point.
     * files_being_uploaded contains a pointer to the same struct as in the files_to_upload
     * queue. So we can only free once.
     */
    int free_lf = TRUE;
    pthread_mutex_lock (&files_being_uploaded_mutex);
    
    for (i = 0; i < g_list_length (files_being_uploaded); i++) {

      char *tmp_lf = g_list_nth_data (files_being_uploaded, i);

      if (strncmp (tmp_lf, lf->name, strlen (tmp_lf)) == 0) {
	files_being_uploaded = g_list_remove (files_being_uploaded, tmp_lf);
        free_single_pointer(tmp_lf);
	break;
      }

    }
    pthread_mutex_unlock (&files_being_uploaded_mutex);

    log_msg (LOG_DEBUG, "Destroying file '%s'\n", lf->cf_name);
    destroy_local_file(lf);
  }
}
