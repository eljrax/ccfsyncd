# what flags you want to pass to the C compiler & linker


# this lists the binaries to produce, the (non-PHONY, binary) targets in
# the previous manual Makefile
bin_PROGRAMS = ccfsyncd
ccfsyncd_CFLAGS = $(DEPS_CFLAGS) $(GLIB_CFLAGS) $(CURL_CFLAGS) $(JANSSON_CFLAGS)
ccfsyncd_LIBS = $(DEPS_LIBS) $(GLIB_LIBS) $(JANSSON_LIBS) $(CURL_LIBS) $(OPENSSL_LIBS) 
ccfsyncd_LDFLAGS = 
ccfsyncd_LDADD = $(DEPS_LIBS) $(GLIB_LIBS) $(CURL_LIBS) $(JANSSON_LIBS) $(OPENSSL_LIBS) $(LDFLAGS) $(GOBJECT_LIBS)
ccfsyncd_SOURCES = ccfsyncd.c cleanup.c compare_remote.c copy_and_remove_file.c delete_file.c doauth.c handle_dir_create.c handle_dir_move.c init_config.c list_files_local.c logging.c misc.c monitor_dir.c signals.c upload_file.c thread_spawn.c excludes.c daemon.c curl_helpers.c ccfsync.h ../config.h
