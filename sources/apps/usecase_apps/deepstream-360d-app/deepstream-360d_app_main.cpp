/*
 * Copyright (c) 2018 NVIDIA Corporation.  All rights reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA Corporation is strictly prohibited.
 *
 */

#include "deepstream-360d_app.hpp"
#include "deepstream_colors.h"
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include "nvds_version.h"

#define MAX_INSTANCES 16

AppCtx *appCtx[MAX_INSTANCES];

static gboolean cintr = FALSE;
static GMainLoop *main_loop = NULL;
static GThread *kb_input_thread = NULL;
static gint return_value = 0;
static gchar **cfg_files = NULL;
static gchar **input_files = NULL;
static GMutex fps_lock;
static gdouble fps[MAX_INSTANCES];
static gdouble fps_avg[MAX_INSTANCES];
static guint num_fps_inst = 0;
static guint num_instances;
static guint num_input_files;
static gboolean quit = FALSE;
static gboolean print_version = FALSE;
static gboolean print_dependencies_version = FALSE;

GOptionEntry entries[] = {
  {"version", 'v', 0, G_OPTION_ARG_NONE, &print_version,
      "Print DeepStreamSDK version", NULL}
  ,
  {"version-all", 0, 0, G_OPTION_ARG_NONE, &print_dependencies_version,
      "Print DeepStreamSDK and dependencies version", NULL}
  ,
  {"cfg-file", 'c', 0, G_OPTION_ARG_FILENAME_ARRAY, &cfg_files,
      "Set the config file", NULL}
  ,
  {"input-file", 'i', 0, G_OPTION_ARG_FILENAME_ARRAY, &input_files,
      "Set the input file", NULL}
  ,
  {NULL}
  ,
};

GST_DEBUG_CATEGORY (NVDS_APP);

static void
_intr_handler (int signum)
{
  struct sigaction action;

  NVGSTDS_ERR_MSG_V ("User Interrupted.. \n");

  memset (&action, 0, sizeof (action));
  action.sa_handler = SIG_DFL;

  sigaction (SIGINT, &action, NULL);

  cintr = TRUE;
}

static void
perf_cb (void *context, NvDsAppPerfStruct * str)
{
  static guint header_print_cnt = 0;
  guint i;
  AppCtx *appCtx = (AppCtx *) context;
  guint numf = num_instances==1 ? str->num_instances:num_instances;
  time_t t = time(NULL);
  struct tm *tm = localtime(&t);
  char curTime[64];
  strftime(curTime, sizeof(curTime), "%c", tm);

  if (header_print_cnt % 20 == 0) {
    g_print ("\n**PERF: \n");
    for (i = 0; i < numf; i++) {
      g_print ("FPS %d (Avg)\t", i);
    }
    g_print ("\n");
    header_print_cnt = 0;
  }
  header_print_cnt++;


  if (num_instances>1) {
    g_mutex_lock (&fps_lock);
    fps[appCtx->index] = str->fps[0];
    fps_avg[appCtx->index] = str->fps_avg[0];
    num_fps_inst++;

    if (num_fps_inst < num_instances || num_instances == 1) {
      g_mutex_unlock (&fps_lock);
      return;
    }
    num_fps_inst = 0;
    g_mutex_unlock (&fps_lock);
  }
  g_print ("**PERF: (%s)\n", curTime);
  for (i=0; i < numf; i++) {
    g_print ("%.2f (%.2f)\t", str->fps[i], str->fps_avg[i]);
  }

  g_print ("\n");
}

/**
 * Loop function to check the status of interrupts.
 * It comes out of loop if application got interrupted.
 */
static gboolean
check_for_interrupt (gpointer data)
{
  if (quit) {
    return FALSE;
  }

  if (cintr) {
    cintr = FALSE;

    quit = TRUE;
    g_main_loop_quit (main_loop);

    return FALSE;
  }
  return TRUE;
}

/*
 * Function to install custom handler for program interrupt signal.
 */
static void
_intr_setup (void)
{
  struct sigaction action;

  memset (&action, 0, sizeof (action));
  action.sa_handler = _intr_handler;

  sigaction (SIGINT, &action, NULL);
}

static gboolean
kbhit (void)
{
  struct timeval tv;
  fd_set rdfs;

  tv.tv_sec = 0;
  tv.tv_usec = 0;

  FD_ZERO (&rdfs);
  FD_SET (STDIN_FILENO, &rdfs);

  select (STDIN_FILENO + 1, &rdfs, NULL, NULL, &tv);
  return FD_ISSET (STDIN_FILENO, &rdfs);
}

/*
 * Function to enable / disable the canonical mode of terminal.
 * In non canonical mode input is available immediately (without the user
 * having to type a line-delimiter character).
 */
static void
changemode (int dir)
{
  static struct termios oldt, newt;

  if (dir == 1) {
    tcgetattr (STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON);
    tcsetattr (STDIN_FILENO, TCSANOW, &newt);
  } else
    tcsetattr (STDIN_FILENO, TCSANOW, &oldt);
}

static void
print_runtime_commands (void)
{
  g_print ("\nRuntime commands:\n"
      "\th: Print this help\n"
      "\tq: Quit\n\n"
      "\tp: Pause\n"
      "\tr: Resume\n\n");
}

/**
 * Loop function to check keyboard inputs and status of each pipeline.
 */
static gboolean
event_thread_func (gpointer arg)
{
  guint i;
  gboolean ret = TRUE;

  // Check if all instances have quit
  for (i = 0; i < num_instances; i++) {
    if (!appCtx[i]->quit)
      break;
  }

  if (i == num_instances) {
    quit = TRUE;
    g_main_loop_quit (main_loop);
    return FALSE;
  }

  // Check for keyboard input
  if (!kbhit ()) {
    //continue;
    return TRUE;
  }
  int c = fgetc (stdin);
  g_print ("\n");

  switch (c) {
    case 'h':
      print_runtime_commands ();
      break;
    case 'p':
      for (i = 0; i < num_instances; i++)
        pause_pipeline (appCtx[i]);
      break;
    case 'r':
      for (i = 0; i < num_instances; i++)
        resume_pipeline (appCtx[i]);
      break;
    case 'q':
      quit = TRUE;
      g_main_loop_quit (main_loop);
      ret = FALSE;
      break;
    default:
      break;
  }
  return ret;
}

int
main (int argc, char *argv[])
{
  GOptionContext *ctx = NULL;
  GOptionGroup *group = NULL;
  GError *error = NULL;
  guint i;

  ctx = g_option_context_new ("Nvidia 360D Demo Application");
  group = g_option_group_new ("abc", NULL, NULL, NULL, NULL);
  g_option_group_add_entries (group, entries);

  g_option_context_set_main_group (ctx, group);
  g_option_context_add_group (ctx, gst_init_get_option_group ());

  if (!g_option_context_parse (ctx, &argc, &argv, &error)) {
    NVGSTDS_ERR_MSG_V ("%s", error->message);
    return -1;
  }

  if (print_version) {
    g_print ("deepstream-360d-app version %d.%d\n",
        NVDS_APP_VERSION_MAJOR, NVDS_APP_VERSION_MINOR);
    nvds_version_print ();
    return 0;
  }

  if (print_dependencies_version) {
    g_print ("deepstream-360d-app version %d.%d\n",
        NVDS_APP_VERSION_MAJOR, NVDS_APP_VERSION_MINOR);
    nvds_version_print ();
    nvds_dependencies_version_print ();
    return 0;
  }

  num_instances = g_strv_length (cfg_files);
  if (input_files)
  {
    num_input_files = g_strv_length (input_files);
  }

  if (!cfg_files || (num_instances==0)) {
    NVGSTDS_ERR_MSG_V ("Specify config file with -c option");
    return_value = -1;
    goto done;
  }

  if (!cfg_files) {
    NVGSTDS_ERR_MSG_V ("Specify config file with -c option");
    return_value = -1;
    goto done;
  }

  for (i = 0; i < num_instances; i++)
  {
    appCtx[i] = (AppCtx *) g_malloc0 (sizeof (AppCtx));
    appCtx[i]->index = i;

    memset(appCtx[i]->category_name, 0, MAX_CATEGORY_LEN);
    sprintf(appCtx[i]->category_name, "DEEPSTREAM_360D_APP_P%d",i);
    GST_DEBUG_CATEGORY_INIT (appCtx[i]->NVDS_APP,
        appCtx[i]->category_name, 0, NULL);

    appCtx[i]->hostname[HOST_NAME_MAX] = 0;
    if (gethostname(appCtx[i]->hostname, HOST_NAME_MAX) == 0)
        g_print("HostName : %s\n", appCtx[i]->hostname);
    else
        perror("gethostname");

    if (input_files && input_files[i]) {
      appCtx[i]->config.multi_source_config[0].uri =
        g_strdup_printf ("file://%s", input_files[i]);
      g_free (input_files[i]);
    }

    if (!parse_config_file (&appCtx[i]->config, cfg_files[i])) {
      NVGSTDS_ERR_MSG_V ("Failed to parse config file '%s'", cfg_files[i]);
      appCtx[i]->return_value = -1;
      goto done;
    }
  }

  for (i = 0; i < num_instances; i++) {
    NVDS_APP = appCtx[i]->NVDS_APP;
    if (!create_pipeline (appCtx[i], NULL,
          NULL, perf_cb)) {
      NVGSTDS_ERR_MSG_V ("Failed to create pipeline");
      return_value = -1;
      goto done;
    }
  }

  main_loop = g_main_loop_new (NULL, FALSE);

  _intr_setup ();
  g_timeout_add (400, check_for_interrupt, NULL);

  for (i = 0; i < num_instances; i++) {
    nvds_meta_pool_init (&appCtx[i]->meta_pool, 50);
    if (gst_element_set_state (appCtx[i]->pipeline.pipeline, GST_STATE_PAUSED) ==
        GST_STATE_CHANGE_FAILURE) {
      NVGSTDS_ERR_MSG_V ("Failed to set pipeline to PAUSED");
      return_value = -1;
      goto done;
    }
  }

  for (i = 0; i < num_instances; i++) {
    if (gst_element_set_state (appCtx[i]->pipeline.pipeline, GST_STATE_PLAYING) ==
        GST_STATE_CHANGE_FAILURE) {

      g_print ("\ncan't set pipeline to playing state.\n");
      return_value = -1;
      goto done;
    }
  }

  print_runtime_commands ();

  changemode (1);

  g_timeout_add (40, event_thread_func, NULL);
  g_main_loop_run (main_loop);
  changemode (0);

done:
  g_print ("Quitting\n");
  for (i = 0; i < num_instances; i++) {
if (appCtx[i]->return_value == -1)
      return_value = -1;
    destroy_pipeline (appCtx[i]);
    nvds_meta_pool_clear (&appCtx[i]->meta_pool);
  }

  if (main_loop) {
    g_main_loop_unref (main_loop);
  }

  if (ctx) {
    g_option_context_free (ctx);
  }

  if (return_value == 0) {
    g_print ("App run successful\n");
  } else {
    g_print ("App run failed\n");
  }

  gst_deinit ();

  return return_value;
}
