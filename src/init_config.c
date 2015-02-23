#include "ccfsync.h"
#include <unistd.h>
#include <stdlib.h>

#include <getopt.h>

#define FREE_SRC TRUE
#define NO_FREE_SRC FALSE


void
overwrite_variable (char **dst, char *src, int free_src)
{
  free_single_pointer (*dst);
  *dst = NULL;
  Sasprintf (*dst, "%s", src);
  if (free_src)
    free_single_pointer (src);
}

void
parse_error (GError * error, gchar * msg)
{
  printf ("%s%s\n", msg == NULL ? "" : msg, error->message);
  g_error_free (error);
  log_msg (LOG_DEBUG, "Exiting in parse_error!\n");
  exit (EXIT_FAILURE);
}


/* have_config determines whether the user supplied a config file or not. 
 * if user-supplied, throw warnings. If we're just looking for the default one
 * it may or may not exist, and either is fine. So exit gracefully 
 */
void
parse_config (int have_config)
{

  GKeyFile *config = g_key_file_new ();
  GError *error = NULL;

  if (!g_key_file_load_from_file (config, cfg->config_file, G_KEY_FILE_NONE, &error)) {
    if (!have_config) {
      g_error_free (error);
      log_msg (LOG_INFO, "No config file supplied, and one doesn't exist in default location...");
      return;
    }

    log_msg (LOG_CRIT, "Failed to read config file %s: %s\n", cfg->config_file, error->message);
    g_error_free (error);
    exit (EXIT_FAILURE);
  }

  log_msg (LOG_DEBUG, "Parsing config file at: %s\n", cfg->config_file);

  /* Get verbosity */
  if (g_key_file_has_key (config, "main", "verbose", &error)) {
    gboolean verbose = g_key_file_get_boolean (config, "main", "verbose", &error);

    if (!verbose && error != NULL)
      parse_error (error, NULL);

    cfg->verbose = verbose;
  }

  /* Get debug */
  if (g_key_file_has_key (config, "main", "debug", &error)) {
    gboolean debug = g_key_file_get_boolean (config, "main", "debug", &error);

    if (!debug && error != NULL)
      parse_error (error, NULL);

    cfg->debug = debug ? TRUE : FALSE;
    cfg->verbose = TRUE;
  }

  /* Get use_syslog */
  if (g_key_file_has_key (config, "main", "use_syslog", &error)) {
    gboolean use_syslog = g_key_file_get_boolean (config, "main", "use_syslog", &error);

    if (!use_syslog && error != NULL)
      parse_error (error, NULL);

    cfg->syslog = use_syslog;

  }


  /* Get foreground */
  if (g_key_file_has_key (config, "main", "foreground", &error)) {
    gboolean foreground = g_key_file_get_boolean (config, "main", "foreground", &error);

    if (!foreground && error != NULL)
      parse_error (error, NULL);

    cfg->foreground = foreground;

  }

  /* Get use_servicenet */
  if (g_key_file_has_key (config, "main", "use_servicenet", &error)) {
    gboolean use_snet = g_key_file_get_boolean (config, "main", "use_servicenet", &error);

    if (!use_snet && error != NULL)
      parse_error (error, NULL);

    cfg->internal_connection = use_snet;

  }

  /* Get API username */
  if (g_key_file_has_key (config, "main", "username", &error)) {
    gchar *username;
    if ((username = g_key_file_get_string (config, "main", "username", &error)) == NULL)
      parse_error (error, NULL);

    overwrite_variable (&cfg->api_username, username, FREE_SRC);
  }

  /* Get API key */

  if (g_key_file_has_key (config, "main", "apikey", &error)) {
    gchar *api_key;
    if ((api_key = g_key_file_get_string (config, "main", "apikey", &error)) == NULL)
      parse_error (error, NULL);

    overwrite_variable (&cfg->api_key, api_key, FREE_SRC);
  }

  /* Get auth endpoint */

  if (g_key_file_has_key (config, "main", "auth-endpoint", &error)) {
    gchar *auth_endpoint;
    if ((auth_endpoint = g_key_file_get_string (config, "main", "auth-endpoint", &error)) == NULL)
      parse_error (error, NULL);

    overwrite_variable (&cfg->auth_endpoint, auth_endpoint, FREE_SRC);
  }

  /* Get region */

  if (g_key_file_has_key (config, "main", "region", &error)) {
    gchar *region;
    if ((region = g_key_file_get_string (config, "main", "region", &error)) == NULL)
      parse_error (error, NULL);

    overwrite_variable (&cfg->region, region, FREE_SRC);
  }

  /* Get container */

  if (g_key_file_has_key (config, "main", "container", &error)) {
    gchar *container;
    if ((container = g_key_file_get_string (config, "main", "container", &error)) == NULL)
      parse_error (error, NULL);

    overwrite_variable (&cfg->container, container, FREE_SRC);
  }

  /* Get monitor_dir */

  if (g_key_file_has_key (config, "main", "monitor_dir", &error)) {
    gchar *monitor_dir;
    if ((monitor_dir = g_key_file_get_string (config, "main", "monitor_dir", &error)) == NULL)
      parse_error (error, NULL);

    overwrite_variable (&cfg->monitor_dir, monitor_dir, FREE_SRC);
  }

  /* Get logfile */

  if (g_key_file_has_key (config, "main", "logfile", &error)) {
    gchar *logfile;
    if ((logfile = g_key_file_get_string (config, "main", "logfile", &error)) == NULL)
      parse_error (error, NULL);

    overwrite_variable (&cfg->log_file, logfile, FREE_SRC);
  }

  /* Get exclusion file */
  if (g_key_file_has_key (config, "main", "exclusion_file", &error)) {
    gchar *exclusion_file;
    if ((exclusion_file = g_key_file_get_string (config, "main", "exclusion_file", &error)) == NULL)
      parse_error (error, NULL);

    overwrite_variable (&cfg->exclusion_file, exclusion_file, FREE_SRC);
  }

  /* Get PID file */

  if (g_key_file_has_key (config, "main", "pid_file", &error)) {
    gchar *pid_file;
    if ((pid_file = g_key_file_get_string (config, "main", "pid_file", &error)) == NULL)
      parse_error (error, NULL);

    overwrite_variable (&cfg->pid_file, pid_file, FREE_SRC);
  }

  /* Get threads (same thread count for all types of threads */
  if (g_key_file_has_key (config, "main", "threads", &error)) {
    gint num_threads = g_key_file_get_integer (config, "main", "threads", &error);
    if (!num_threads && error != NULL)
      parse_error (error, NULL);
    cfg->num_upload_threads = num_threads;
    cfg->num_delete_threads = num_threads;
    cfg->num_copy_threads = num_threads;
  }

  /* Get specific thread count - overriding the overall one */
  if (g_key_file_has_key (config, "main", "upload_threads", &error)) {
    gint num_threads = g_key_file_get_integer (config, "main", "upload_threads", &error);
    if (!num_threads && error != NULL)
      parse_error (error, NULL);
    cfg->num_upload_threads = num_threads;
  }

  /* Get specific thread count - overriding the overall one */
  if (g_key_file_has_key (config, "main", "delete_threads", &error)) {
    gint num_threads = g_key_file_get_integer (config, "main", "delete_threads", &error);
    if (!num_threads && error != NULL)
      parse_error (error, NULL);
    cfg->num_delete_threads = num_threads;
  }

  /* Get specific thread count - overriding the overall one */
  if (g_key_file_has_key (config, "main", "copy_threads", &error)) {
    gint num_threads = g_key_file_get_integer (config, "main", "copy_threads", &error);
    if (!num_threads && error != NULL)
      parse_error (error, NULL);
    cfg->num_copy_threads = num_threads;
  }

  if (error != NULL)
    g_error_free (error);
  g_key_file_free (config);
}


/* This *needs* to be called before signal handlers are put into place. Because we suicide() here without having
 * and pthreads joined in main() 
 */
void
init_config (int argc, char *argv[])
{

  cfg = malloc (sizeof (config));

/* Init and set safe defaults */
  cfg->auth_endpoint = NULL;
  cfg->api_username = NULL;
  cfg->api_key = NULL;
  cfg->region = NULL;
  cfg->container = NULL;
  cfg->monitor_dir = NULL;
  cfg->log_file = NULL;
  cfg->config_file = NULL;
  cfg->exclusion_file = NULL;
  cfg->pid_file = NULL;

  Sasprintf (cfg->auth_endpoint, "https://identity.api.rackspacecloud.com/v2.0/tokens/");
  /* Region is not really used, since Rackspace now has global auth */
  Sasprintf (cfg->region, "LON");
  Sasprintf (cfg->config_file, "/etc/%s.conf", PACKAGE_NAME);
  Sasprintf (cfg->pid_file, "/var/run/%s.pid", PACKAGE_NAME);

  cfg->syslog = TRUE;
  cfg->verbose = FALSE;
  cfg->num_upload_threads = cfg->num_delete_threads = cfg->num_copy_threads = 5;
  cfg->foreground = FALSE;
  cfg->internal_connection = TRUE;
  cfg->debug = FALSE;

  int c;
  /* Indicates whether user supplied a config file or not */
  int have_config = FALSE;
  int config_parsed = FALSE;
  int option_index = 0;
  int tmp_threads = -1;

  int got_quit = FALSE;
  while (1) {

    static struct option long_options[] = {
      {"verbose", no_argument, 0, 'v'},
      {"auth-endpoint", required_argument, 0, 'a'},
      {"username", required_argument, 0, 'u'},
      {"api-key", required_argument, 0, 'k'},
      {"region", required_argument, 0, 'r'},
      {"container", required_argument, 0, 'c'},
      {"local-dir", required_argument, 0, 'd'},
      {"log-file", required_argument, 0, 'l'},
      {"no-syslog", no_argument, 0, 'n'},
      {"upload-threads", required_argument, 0, 'x'},
      {"delete-threads", required_argument, 0, 'z'},
      {"copy-threads", required_argument, 0, 'y'},
      {"config-file", required_argument, 0, 'f'},
      {"foreground", no_argument, 0, 'g'},
      {"no-service-net", no_argument, 0, 's'},
      {"threads", required_argument, 0, 't'},
      {"help", no_argument, 0, 'h'},
      {"debug", no_argument, 0, 'b'},
      {"pid-file", no_argument, 0, 'p'},
      {"quit", no_argument, 0, 'q'},
      {"exclusion-file", required_argument, 0, 'e'},
      {0, 0, 0, 0}
    };


    /* First check whether we've got a config file that'll override defaults, but may be overridden by CLI args */
    while (!have_config) {
      int i;
      for (i = 0; i < argc; i++) {

	if ((strncmp (argv[i], "-f", 3) == 0) || strncmp (argv[i], "--config-file", strlen ("--config-file") + 1) == 0) {

	  if (argc < i + 2) {
	    suicide ("-f and --config-file requires an argument!\n");
	  }

	  else {
	    overwrite_variable (&cfg->config_file, argv[i + 1], NO_FREE_SRC);
	    have_config = TRUE;
	  }
	}
      }
      break;
    }

    if (!config_parsed) {
      parse_config (have_config);
      config_parsed = TRUE;
    }
    have_config = TRUE;

    c = getopt_long (argc, argv, "bhva:u:k:r:c:d:l:nx:y:z:f:t:e:gp:qs", long_options, &option_index);

    /* Detect the end of the options. */
    if (c == -1) {
      break;
    }

    switch (c) {
    case 0:
      /* If this option set a flag, do nothing else now. */
      if (long_options[option_index].flag != 0) {
	break;
      }
      printf ("option %s", long_options[option_index].name);
      if (optarg)
	printf (" with arg %s", optarg);
      printf ("\n");
      break;

    case 'v':
      cfg->verbose = TRUE;
      break;
    case 'b':
      cfg->debug = TRUE;
      cfg->verbose = TRUE;
      break;
    case 'h':
      help (argv[0]);
      break;
    case 'a':
      overwrite_variable (&cfg->auth_endpoint, optarg, NO_FREE_SRC);
      break;
    case 'g':
      cfg->foreground = TRUE;
      break;
    case 'u':
      overwrite_variable (&cfg->api_username, optarg, NO_FREE_SRC);
      break;
    case 'f':
      /* We've already dealt with this above as a special case - so NOOP */
      break;
    case 'k':
      overwrite_variable (&cfg->api_key, optarg, NO_FREE_SRC);
      break;
    case 'p':
      overwrite_variable (&cfg->pid_file, optarg, NO_FREE_SRC);
      break;
    case 'r':
      overwrite_variable (&cfg->region, optarg, NO_FREE_SRC);
      break;
    case 'c':
      overwrite_variable (&cfg->container, optarg, NO_FREE_SRC);
      break;
    case 'd':
      overwrite_variable (&cfg->monitor_dir, optarg, NO_FREE_SRC);
      break;
    case 'l':
      overwrite_variable (&cfg->log_file, optarg, NO_FREE_SRC);
      break;
    case 'n':
      cfg->syslog = FALSE;
      break;
    case 's':
      cfg->internal_connection = FALSE;
      break;
    case 'e':
      overwrite_variable (&cfg->exclusion_file, optarg, NO_FREE_SRC);
      break;
    case 'q':
      /* We need to delay the quitting, in case we get a PID file path or 
       * verbosity is set, we need to know that going into terminate_process ()
       */
      got_quit = TRUE;
      break;
    case 't':
      tmp_threads = char_to_pos_int (optarg);
      if (tmp_threads < 0) {
	suicide ("Number of threads must be a positive integer. Given: %s\n", optarg);
      }
      cfg->num_upload_threads = cfg->num_delete_threads = cfg->num_copy_threads = tmp_threads;
      break;
    case 'x':
      tmp_threads = char_to_pos_int (optarg);
      if (tmp_threads < 0) {
	suicide ("Number of upload threads must be a positive integer. Given: %s\n", optarg);
      }
      cfg->num_upload_threads = tmp_threads;
      break;
    case 'y':
      tmp_threads = char_to_pos_int (optarg);
      if (tmp_threads < 0) {
	suicide ("Number of copy threads must be a positive integer. Given: %s\n", optarg);
      }
      cfg->num_copy_threads = tmp_threads;
      break;
    case 'z':
      tmp_threads = char_to_pos_int (optarg);
      if (tmp_threads < 0) {
	suicide ("Number of delete threads must be a positive integer. Given: %s\n", optarg);
      }
      cfg->num_delete_threads = tmp_threads;
      break;
    default:
      help (argv[0]);
      break;
    }

  }


  if (got_quit)
    terminate_process ();

  if (cfg->verbose) {
    printf ("Finished parsing config and command line arguments: \n\n");
    printf ("Verbose is enabled\n");
    if (cfg->foreground)
      printf ("Staying in foreground\n");
    else
      printf ("NOT staying in foreground\n");
    if (cfg->internal_connection)
      printf ("Using internal connection\n");
    else
      printf ("NOT using internal connection\n");
    if (cfg->syslog)
      printf ("Using syslog\n");
    else
      printf ("NOT using syslog\n");
    if (cfg->log_file)
      printf ("Log file = '%s'\n", cfg->log_file);
    else
      printf ("Not logging to file\n");
    printf ("Username = '%s'\n", cfg->api_username);
    printf ("API key = '%s'\n", cfg->api_key);
    printf ("Auth endpoint: '%s'\n", cfg->auth_endpoint);
    printf ("Region = '%s'\n", cfg->region);
    printf ("Container = '%s'\n", cfg->container);
    printf ("Local dir = '%s'\n", cfg->monitor_dir);
    printf ("Config file = '%s'\n", cfg->config_file);
    printf ("Upload threads = %d\n", cfg->num_upload_threads);
    printf ("Delete threads = %d\n", cfg->num_delete_threads);
    printf ("Copy threads = %d\n", cfg->num_copy_threads);
    printf ("PID file = %s\n", cfg->pid_file);
    if (cfg->exclusion_file)
      printf ("Exclusions file =  %s\n", cfg->exclusion_file);
  }

  /* May not return if we do not have everything we need to get going */
  validate_config ();

  return;
}
