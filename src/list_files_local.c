
#include "ccfsync.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <glib.h>
#include <glib-object.h>
#include <openssl/md5.h>

#define HASH_CHUNK_SIZE 65000

GList *
listdir (char *name, char *parent, int level, struct exclusions * exclusions)
{
  DIR *dir;
  struct dirent *entry;
  GList *file_list = NULL;

  if (!(dir = opendir (name))) {
    log_msg (LOG_CRIT, "Failed to open directory %s for listing", name);
    return NULL;
  }
  if (!(entry = readdir (dir))) {
    log_msg (LOG_CRIT, "Failed to read directory %s", name);
    return NULL;
  }

  do {

    if (strcmp (entry->d_name, ".") == 0 || strcmp (entry->d_name, "..") == 0) {
      continue;
    }

    gchar *fullpath = NULL;
    Sasprintf (fullpath, "%s/%s", name, entry->d_name);

    if (regex_match (fullpath, exclusions)) {
      free_single_pointer (fullpath);
      continue;
    }

    /* XFS doesn't set d_type, it's always 0 - need to stat */
    /* if (entry->d_type == DT_DIR) { */
    
    struct stat st;
    if (stat(fullpath, &st) < 0){ 
      free_single_pointer (fullpath);
      continue;
    }
    if (S_ISDIR(st.st_mode)){
      GList *tmp = NULL;
      tmp = listdir (fullpath, parent, level + 1, exclusions);
      free_single_pointer (fullpath);

      unsigned int i;
      /* Copy the list we obtained from the recursion into the one we're returning */
      for (i = 0; i < g_list_length (tmp); i++) {
	gchar *tmp_data = g_list_nth_data (tmp, i);
	file_list = g_list_prepend (file_list, g_strdup (tmp_data));
	free_single_pointer (tmp_data);
      }
      g_list_free (tmp);
    }
    else {
      file_list = g_list_prepend (file_list, fullpath);
    }
  } while ((entry = readdir (dir)));

  closedir (dir);

  return file_list;
}


GHashTable *
stat_local_files (GList * files, gchar * base_dir, struct exclusions * exclusions)
{
  GHashTable *local_files = g_hash_table_new_full (g_str_hash, g_str_equal, (GDestroyNotify) free_single_pointer, NULL);
  unsigned int i;

  for (i = 0; i < g_list_length (files); i++) {
    char *file = g_list_nth_data (files, i);
    if (regex_match (file, exclusions)) {
      continue;
    }
    local_file *lf = stat_local_file (file, base_dir);
    if (lf != NULL)
      g_hash_table_insert (local_files, g_strdup ((char *) lf->name), lf);
  }
  return local_files;
}

/* Gathers information about a single file on the filesystem */
local_file *
stat_local_file (gchar * file, gchar * base_dir)
{

  local_file *lf = malloc (sizeof (local_file));
  lf->st = malloc (sizeof (struct stat));
  /* Turn /data/path/file into file */
  lf->cf_name = g_strdup (file + strlen (base_dir) + 1);
  lf->name = g_strdup ((char *) file);
  lf->sentinel = g_strdup ("ok");

  int bytes;
  unsigned char data[HASH_CHUNK_SIZE];
  unsigned char c[MD5_DIGEST_LENGTH + 1];
  char c_buf[MD5_DIGEST_LENGTH + 1];
  /* Because MD5.... ..... */
  char hash_copy[MD5_DIGEST_LENGTH * 2 + 1];

  memset (hash_copy, 0, sizeof (hash_copy));

  FILE *fp = fopen ((char *) file, "rb");
  if (!fp) {
    log_msg (LOG_WARNING, "Failed to read file '%s' for hashing. Do you have read permissions? Or did it live a very short life?\n", (char *) file);
    free_single_pointer (lf->name);
    free_single_pointer (lf->cf_name);
    free_single_pointer (lf->st);
    free_single_pointer (lf->sentinel);
    free_single_pointer (lf);
    free_single_pointer (file);
    return NULL;
  }


  MD5_CTX mdContext;
  MD5_Init (&mdContext);
  while ((bytes = fread (data, 1, HASH_CHUNK_SIZE, fp)) != 0)
    MD5_Update (&mdContext, data, bytes);
  fclose (fp);
  MD5_Final (c, &mdContext);
  c[MD5_DIGEST_LENGTH] = '\0';
  int j;
  for (j = 0; j < MD5_DIGEST_LENGTH; j++) {
    snprintf (c_buf, sizeof (c_buf), "%02x", c[j]);
    strncat (hash_copy, c_buf, strlen (c_buf));
  }
  lf->hash = g_strdup (hash_copy);


  if ((stat ((char *) file, lf->st)) < 0) {
    log_msg (LOG_WARNING, "In stat_local_file: Failed to stat file %s: %s", lf->name, strerror (errno));
    free_single_pointer (lf->name);
    free_single_pointer (lf->cf_name);
    free_single_pointer (lf->sentinel);
    free_single_pointer (lf->hash);
    free_single_pointer (lf->st);
    free_single_pointer (lf);
    free_single_pointer (file);
    return NULL;
  }
  free_single_pointer (file);

  return lf;
}


GHashTable *
list_files_local (char *dir, char *monitor_dir, struct exclusions * exclusions)
{
  GList *files = NULL;
  files = listdir (dir, dir, 0, exclusions);
  if (files == NULL)
    return NULL;
  GHashTable *local_files = stat_local_files (files, monitor_dir, exclusions);
  if (local_files == NULL)
    return NULL;

  g_list_free (files);

  return local_files;
}
