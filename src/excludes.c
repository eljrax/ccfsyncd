#include "ccfsync.h"
#include <ctype.h>

int
is_comment (char *str)
{
  int i;
  int len = strlen (str);
  for (i = 0; i < len; i++) {
    if (str[i] != '#' && str[i] != ';' && !isspace (str[i]))
      return FALSE;
    else if (isspace (str[i]))
      continue;
    else
      return TRUE;
  }
  return TRUE;
}


struct exclusions *
init_exclusions ()
{

  if (!cfg->exclusion_file)
    return NULL;
  FILE *fp;
  char *buf = NULL;
  struct exclusions *exclusions = malloc (sizeof (struct exclusions));
  exclusions->len = 0;
  exclusions->capacity = 10;
  exclusions->exex = malloc (sizeof (regex_t) * exclusions->capacity);
  ssize_t read;
  size_t len;
  int idx = 0, lineno = 0, ret;

  if ((fp = fopen (cfg->exclusion_file, "r")) == NULL)
    suicide ("Failed to open exclusion file %s: %s", cfg->exclusion_file, strerror (errno));

  while ((read = getline (&buf, &len, fp)) != -1) {

    if (is_comment (buf)) {
      lineno++;
      continue;
    }

    strip_char (buf, '\n');

    /* Pre-allocate regex structures in buckets of 10 */
    if (exclusions->len == exclusions->capacity) {
      exclusions->exex = realloc (exclusions->exex, sizeof (regex_t) * (exclusions->len + 10));
      exclusions->capacity += 10;
    }

    exclusions->exex[exclusions->len] = malloc (sizeof (regex_t));
    ret = regcomp (exclusions->exex[exclusions->len], buf, REG_EXTENDED);
    if (ret != 0) {
      suicide ("Compilation failed for exclusion regex: %s on line %d in file %s", buf, lineno, cfg->exclusion_file);
    }
    exclusions->len++;

    idx++;
    lineno++;

  }
  if (buf)
    free_single_pointer (buf);

  log_msg (LOG_DEBUG, "%d exclusion regexes read\n", exclusions->len);
  return exclusions;
}

/* Returns true if we have an exclusion for the file in question
 */
int
regex_match (gchar * str, struct exclusions *exclusions)
{

  if (!cfg->exclusion_file)
    return FALSE;
  int i;
  int ret;
  for (i = 0; i < exclusions->len; i++) {
    ret = regexec (exclusions->exex[i], str, 0, NULL, 0);
    if (ret == 0) {
      return TRUE;
    }
  }
  return FALSE;
}
