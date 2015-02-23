#include "ccfsync.h"

struct thread_inventory *
spawn_threads ()
{

  struct thread_inventory *thread_inventory = malloc ((sizeof (struct thread_inventory) + sizeof (sizeof (pthread_t))) * MAX_THREADS);
  /* Threads handling uploads */
  int i, rc;

  int total_num_threads = cfg->num_upload_threads + cfg->num_delete_threads + cfg->num_copy_threads;
  /* Threads themselves will free these as they exit */
  thread_data *td[total_num_threads];
  int td_index = 0;

  for (i = 0; i < cfg->num_upload_threads; i++) {
    td[td_index] = malloc (sizeof (thread_data));
    td[td_index]->thread_id = i;
    rc = pthread_create (&thread_inventory->upload_thread[i], NULL, upload_file, td[td_index]);
    if (rc != 0) {
      log_msg (LOG_CRIT, "Failed to spawn upload thread #%d. Error code: %d\n", i, rc);
      exit (EXIT_FAILURE);
    }
    td_index++;
  }


  /* Threads handling deletions */
  for (i = 0; i < cfg->num_delete_threads; i++) {
    td[td_index] = malloc (sizeof (thread_data));
    td[td_index]->thread_id = i;
    rc = pthread_create (&thread_inventory->delete_thread[i], NULL, delete_file, td[td_index]);
    if (rc != 0) {
      log_msg (LOG_CRIT, "Failed to spawn delete thread #%d. Error code: %d\n", i, rc);
      exit (EXIT_FAILURE);
    }
    td_index++;
  }

  /* Threads handling copy (triggered by file move events) */
  for (i = 0; i < cfg->num_copy_threads; i++) {
    td[td_index] = malloc (sizeof (thread_data));
    td[td_index]->thread_id = i;
    rc = pthread_create (&thread_inventory->copy_thread[i], NULL, copy_file_and_remove, td[td_index]);
    if (rc != 0) {
      log_msg (LOG_CRIT, "Failed to spawn delete thread #%d. Error code: %d\n", i, rc);
      exit (EXIT_FAILURE);
    }
    td_index++;
  }
  return thread_inventory;
}

void
wait_threads (struct thread_inventory *thread_inventory)
{
  int i;
  for (i = 0; i < cfg->num_upload_threads; i++) {
    pthread_join (thread_inventory->upload_thread[i], NULL);
    log_msg (LOG_DEBUG, "Upload thread %d has exited...", i);
  }
  for (i = 0; i < cfg->num_delete_threads; i++) {
    pthread_join (thread_inventory->delete_thread[i], NULL);
    log_msg (LOG_DEBUG, "Delete thread %d has exited...", i);
  }
  for (i = 0; i < cfg->num_copy_threads; i++) {
    pthread_join (thread_inventory->copy_thread[i], NULL);
    log_msg (LOG_DEBUG, "Copy thread %d has exited...", i);
  }

}
