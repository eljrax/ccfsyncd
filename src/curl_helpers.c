#include "ccfsync.h"

size_t 

curl_devnull (void *ptr, size_t size, size_t nmemb, void *arg){

  return size * nmemb;

}
size_t
write_data (void *ptr, size_t size, size_t nmemb, void *arg)
{
  struct string *s = arg;
  size_t new_len = s->len + size * nmemb;
  s->data = realloc (s->data, new_len + 1);
  if (s->data == NULL) {
    log_msg (LOG_CRIT, "realloc() failed\n");
    exit (EXIT_FAILURE);
  }
  memcpy (s->data + s->len, ptr, size * nmemb);
  s->data[new_len] = '\0';
  s->len = new_len;

  return size * nmemb;
}

size_t
write_data_file (void *ptr, size_t size, size_t nmemb, FILE * stream)
{
  size_t written = fwrite (ptr, size, nmemb, stream);
  return written;
}


void
handle_http_error (int http_code)
{
  switch (http_code) {
  case 401:
    auth->auth_msg = "Failed to authenticate. Wrong username or API key. HTTP error code: 401";
    break;
  case 404:
    auth->auth_msg = "Failed to authenticate. Wrong URL? HTTP error code: 404";
    break;
  case 405:
    auth->auth_msg = "Failed to authenticate. HTTP error code: 405";
    break;
  case 302:
    auth->auth_msg = "Got redirect back. Are you using the correct auth endpoint? HTTP instead of HTTPS? HTTP code: 302";
    break;
  default:
    auth->auth_msg = "Unknown error occurred.";
    break;
  }


}

