#include "ccfsync.h"


void
init_string (struct string *s)
{
  s->len = 0;
  s->data = malloc (s->len + 1);
  if (s->data == NULL) {
    log_msg (LOG_ERR, "malloc() failed\n");
    exit (EXIT_FAILURE);
  }
  s->data[0] = '\0';
}

int
init_auth ()
{
  auth = malloc (sizeof (struct auth));
  auth->token_header = NULL;
  doAuth (FIRST_AUTH);
  return 0;
}


void 
get_token (char *authResp, int first_auth)
{

  gchar *ret;
  json_t *root, *access, *token, *id, *token_val;
  json_error_t error;


  root = json_loads (authResp, 0, &error);

  if (!root) {
    log_msg (LOG_ERR, "In get_token: failed to load root of JSON response: %s\n", error.text);
    exit (1);
  }

  if (!json_is_object (root)) {
    log_msg (LOG_ERR, "get_token: Malformed JSON back from endpoint: root is not an object\n");
    json_decref (root);
    auth->token = NULL;
    return;
  }

  if ((access = json_object_get (root, "access")) == NULL){
    log_msg (LOG_ERR, "get_token: Malformed JSON back from endpoint: access is not an object\n");
    auth->token = NULL;
    return;
  }

  if ((token = json_object_get (access, "token")) == NULL){
    log_msg (LOG_ERR, "get_token: Malformed JSON back from endpoint: token is not an object\n");
    auth->token = NULL;
    return;
  }

  if ((id = json_object_get (token, "id")) == NULL){
    log_msg (LOG_ERR, "get_token: Malformed JSON back from endpoint: id is not an object\n");
    auth->token = NULL;
    return;
  }

  
  /* If we're reauthenticating, we'll already have a token  */
  if (!first_auth) {
    free_single_pointer (auth->token);
  }
  auth->token = strdup (json_string_value (id));

  json_decref (root);
  log_msg(LOG_DEBUG, "Token acquired: %s", auth->token);

}


void
get_endpoint (char *authResp, int first_auth)
{
  gchar *ret;
  json_t *root, *access, *serviceCatalog, *endpoints, *enditr, *endpoint;
  json_error_t error;

  root = json_loads (authResp, 0, &error);

  if (!root) {
    log_msg (LOG_ERR, "get_endpoint: Malformed JSON back from endpoint: Failed to load root of JSON response");
    exit (1);
  }

  if (!json_is_object (root)) {
    log_msg (LOG_ERR, "get_endpoint: Malformed JSON back from endpoint: root is not an object");
    json_decref (root);
    auth->endpoint = NULL;
    return;
  }

  access = json_object_get (root, "access");
  if (!json_is_object (access)) {
    log_msg (LOG_ERR, "get_endpoint: Malformed JSON back from endpoint: access is not a JSON object!");
    json_decref (root);
    auth->endpoint = NULL;
    return;
  }

  serviceCatalog = json_object_get (access, "serviceCatalog");
  if (!json_is_array (serviceCatalog)) {
    log_msg (LOG_ERR, "get_endpoint: Malformed JSON back from endpoint: serviceCatalog is not a JSON object");
    json_decref (root);
    auth->endpoint = NULL;
    return;
  }

  unsigned int i;
  for (i = 0; i < json_array_size (serviceCatalog); i++) {
    json_t *service, *name;
    service = json_array_get (serviceCatalog, i);
    if (!json_is_object (service)) {
      continue;
    }

    name = json_object_get (service, "name");
    if (!json_is_string (name)) {
      log_msg (LOG_ERR, "get_endpoint: Malformed JSON back from endpoint: endpoint name is not a string!");
      auth->endpoint = NULL;
      return;
    }

    if (strncmp (json_string_value (name), "cloudFiles", strlen (json_string_value (name))) == 0) {

      endpoints = json_object_get (service, "endpoints");
      if (!json_is_array (endpoints)) {
	log_msg (LOG_ERR, "get_endpoint: Malformed JSON back from endpoint: Unable to obtain endpoint from JSON response");
        auth->endpoint = NULL;
	return;
      }

      unsigned int j;
      for (j = 0; j < json_array_size (endpoints); j++) {
	enditr = json_array_get (endpoints, j);
	if (cfg->internal_connection) {
	  endpoint = json_object_get (enditr, "internalURL");
	}
	else
	  endpoint = json_object_get (enditr, "publicURL");
      }
      if (endpoint == NULL) {
	log_msg (LOG_ERR, "get_endpoint: Malformed JSON back from endpoint: Unable to obtain endpoint from JSON response");
        auth->endpoint = NULL;
	return;
      }
      
      /* If we're reauthenticating, we'll already have an endpoint  */
      if (!first_auth) {
        free_single_pointer (auth->endpoint);
      }
      auth->endpoint = g_strdup (json_string_value (endpoint));
      log_msg (LOG_DEBUG, "CF endpoint selected: %s", auth->endpoint);
      
    }
  }

  json_decref (root);
}

void
doAuth (int first_auth)
{

  CURL *curl;
  CURLcode res;
  struct curl_slist *headerlist = NULL;
  long http_code = 0;

  struct string auth_ret;
  init_string (&auth_ret);

  /* Use this to communicate state to calling function */
  auth->auth_msg = NULL;
  gchar *ret;

  char *reqData = NULL;
  Sasprintf (reqData, "{ \"auth\": { \"RAX-KSKEY:apiKeyCredentials\": { \"username\": \"%s\", \"apiKey\": \"%s\" } } }", cfg->api_username, cfg->api_key);


  curl = curl_easy_init ();
  if (!curl) {
    suicide ("Failed to init curl: %s\n", strerror (errno));
  }

  headerlist = curl_slist_append (headerlist, (const char *) "Content-type: application/json");
  headerlist = curl_slist_append (headerlist, (const char *) "Accept: application/json");

//  curl_easy_setopt (curl, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt (curl, CURLOPT_POSTFIELDSIZE, (long) strlen (reqData));
  curl_easy_setopt (curl, CURLOPT_HTTPHEADER, headerlist);
  curl_easy_setopt (curl, CURLOPT_URL, cfg->auth_endpoint);
  curl_easy_setopt (curl, CURLOPT_POSTFIELDS, reqData);
  curl_easy_setopt (curl, CURLOPT_WRITEFUNCTION, write_data);
  curl_easy_setopt (curl, CURLOPT_WRITEDATA, &auth_ret);

  res = curl_easy_perform (curl);

  if (res != CURLE_OK) {
    curl_easy_cleanup (curl);
    suicide ("Error performing request: %s Potential HTTP error code: %d\n", curl_easy_strerror (res), (int) http_code);
  }

  curl_easy_getinfo (curl, CURLINFO_RESPONSE_CODE, &http_code);

  if ((int) http_code != 200) {
    handle_http_error (http_code);
    suicide (auth->auth_msg);
  }

  free_single_pointer (reqData);
  ret = auth_ret.data;

  if (ret == NULL) {
    suicide ("Failed to read response from authentication endpoint..");
  }

  curl_slist_free_all (headerlist);
  curl_easy_cleanup (curl);


  get_token (ret, first_auth);
  get_endpoint (ret, first_auth);
  
  if (auth->endpoint == NULL || auth->token == NULL){
    suicide (auth->auth_msg);
  }

  free_single_pointer (ret);

  if (!first_auth){
    free_single_pointer(auth->token_header);
    auth->token_header = NULL;
  }
  Sasprintf(auth->token_header, "X-Auth-Token: %s", auth->token);
    
  return;
}
