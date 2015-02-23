
#ifndef CCFSYNC_H
#define CCFSYNC_H

#define _GNU_SOURCE

#include "../config.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <glib.h>
#include <glib-object.h>
#include <glib/gthread.h>
#include <curl/curl.h>
#include <glib/gqueue.h>
#include <jansson.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <syslog.h>
#include <pthread.h>
#include <dirent.h>
#include <regex.h>

/* Maximum number of threads PER thread-type. */ 
#define MAX_THREADS 10

#define LOG_MEMDEBUG LOG_DEBUG+1
struct string {
  size_t len;
  char *data;
};

struct auth {
  gchar *token;
  gchar *token_header;
  gchar *endpoint;
  gchar *auth_msg;
};

struct cf_file {
  size_t len;
  gchar *name;
  gchar *content_type;
  gchar *hash;
  gchar *local_path;
  /* When a struct where sentinel has the value 'exit', the delete thread will exit. Only used for internal purposes */ 
  gchar *sentinel;
  size_t size;
  time_t last_modified;
};

typedef struct cf_file cf_file;

struct local_file {
  gchar *name;
  /* Corresponding name on CloudFiles to handle
  * things like /dir/anotherdir/file */
  gchar *cf_name;
  gchar *hash;
  /* When a struct where sentinel has the value 'exit', the upload thread will exit. Only used for internal purposes */ 
  gchar *sentinel;
  struct stat *st;
};

typedef struct local_file local_file;

struct thread_data {
    int thread_id;
    gchar *dummy_ptr;
};

struct exclusions {
  /* Number of exclusions we have */
  int len;
  int capacity;
  regex_t **exex; 
};

struct cf_file_copy {
  gchar *old_name;
  gchar *new_name;
  gchar *sentinel;
  cf_file *cf_file;
};


struct move_event
{
  unsigned int cookie;
  gchar *event_name;
  gchar *cf_name;
  gchar *full_local_path;
};

struct move_thread_data {
  struct move_event *me;
  int fd;
  int events_mask;
  GHashTable *watches;
  gchar *tmp_path;
  gchar *cf_tmp_path;
  struct monitor_dir_data *md;
  struct inotify_event *ev;
  struct exclusions *exclusions;
};


struct config {
  char *auth_endpoint;
  gchar *api_username;
  gchar *api_key;
  gchar *region;
  gchar *container;
  gchar *monitor_dir;
  gchar *log_file;
  gchar *config_file;
  gchar *exclusion_file;
  gchar *pid_file;
  int num_upload_threads;
  int num_delete_threads;
  int num_copy_threads;
  int foreground;
  int internal_connection;
  int syslog;
  int verbose;
  int debug;
};

typedef struct config config;
typedef struct thread_data thread_data;
typedef struct cf_file_copy cf_file_copy;

struct thread_inventory {
  pthread_t upload_thread[MAX_THREADS];
  pthread_t delete_thread[MAX_THREADS];
  pthread_t copy_thread[MAX_THREADS];

};

extern FILE *log_fp;
extern struct auth *auth;
extern config *cfg;
/* Global thread-safe structures */
extern pthread_mutex_t auth_in_progress_mutex;
extern pthread_mutex_t watches_mutex;
extern pthread_mutex_t move_events_mutex;
extern pthread_mutex_t files_being_uploaded_mutex;
extern GList *files_being_uploaded;
extern GList *move_events;
extern GAsyncQueue *files_to_upload;
extern GAsyncQueue *files_to_delete;
extern GAsyncQueue *files_to_copy;
extern int exiting;
extern int threaded;


void list_files_cf(GHashTable **cf_files, gchar *marker, struct exclusions *exclusions);
int get_cf_files_list (gchar * marker, struct string **resp);
void get_token(char *authResp, int first_auth);
size_t curl_devnull (void *ptr, size_t size, size_t nmemb, void *arg);
void doAuth(int auth_type);
void get_endpoint(char *authResp, int first_auth);
GHashTable *list_files_local (char *dir, char *monitor_dir, struct exclusions *exclusions);
/* Compare files on local that's not on remote, returns a list of files to be uploaded. Populate global GQueues */
GList *compare_remote(GHashTable *local, GHashTable *remote);
void compare_local(GHashTable *remote, GHashTable *local);
void handle_http_error(int http_code);
void destroy_local_file(gpointer item);
void daemonise();
void destroy_move_event(struct move_event *me);
void destroy_cf_file(gpointer item, gpointer user_data);
void cleanup_globals();
void destroy_exclusions(struct exclusions *exclusions);
void destroy_cf_file_copy(gpointer item);
void destroy_logging();
void free_single_pointer(gpointer item);
void *upload_file(void* data);
void strip_char(char* str, char c);
void *delete_file(void* data);
void suidice(gchar *msg);
void *handle_dir_move(void *data);
struct thread_inventory *spawn_threads();
local_file *stat_local_file(gchar *file, gchar *base_dir);
int regex_match (gchar *str, struct exclusions *exclusions);
void *monitor_dir_inotify ();
void signal_handler(int sig);
cf_file *build_cf_file_from_lf(gchar *name);
GList *get_dirs(gchar *name, gchar *parent);
void free_lfs(GList *to_be_free, GHashTable *local);
void free_cfs(GList *to_be_free, GHashTable *remote);
void suicide(gchar *fmt, ...);
void *copy_file_and_remove(void *data);
GList *get_cf_files_from_dir(gchar *dir, struct exclusions *exclusions);
/* Frees the global list containing files in queue for upload */
void destroy_files_being_uploaded();
void *handle_dir_create(void *data);
int add_watches_recursively(char *dir, int inotify_fd, GHashTable **watches, int monitor_events );
size_t write_data (void *ptr, size_t size, size_t nmemb, void *arg);
void signal_ignore(int sig);
void init_string (struct string *s);
int init_auth();
void init_config (int argc, char *argv[]);
int char_to_pos_int(gchar *str);
void help(char *binary_name);
struct exclusions *init_exclusions();
void validate_config();
void init_logging();
int log_msg(int level, char *fmt, ...);
void drain_queue(GAsyncQueue *queue);
void wait_threads (struct thread_inventory *thread_inventory);
int delete_local_file (char *file);
void terminate_process();


#ifndef FALSE
#define FALSE (0)
#endif
#ifndef TRUE
#define TRUE (!FALSE)
#endif

#define FIRST_AUTH TRUE
#define REAUTH FALSE


#define Sasprintf(write_to, ...) { \
  char *tmp_string_for_extend = (write_to); \
    if (asprintf(&(write_to), __VA_ARGS__) < 0) { } \
      free(tmp_string_for_extend); \
}

#endif

