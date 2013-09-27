#pragma execution_character_set("utf-8")
#include <gst/gst.h>
#include <fcntl.h>
#include <io.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <winsock2.h>
#include <direct.h>

/* macro */
#define STREAM_AMOUNT 5           // ストリーム元数
#define POLLING_SECOND 20         // ポート監視間隔
#define TAGENYA_VERSION "0.0.99"	// バージョン
#define LATENCY 20000000000			  // default latency in nanoseconds
#define BUFFER_TIMEOUT 30000000000  // buffer timeout amount in nanoseconds
#define CLOCK_TIMEOUT 60          // clock timeout amount in seconds

#define MMS_HOST "localhost"
#define MMS_BASE_PORT 47000
#define MMS_EX_PORT 48000
#define HTTP_OK 200 // httpresponsecode200
#define HTTP_BUSY 503 //

#define MMSH_STATUS_NULL 0
#define MMSH_STATUS_CONNECTED 1
#define MMSH_STATUS_HTTP_HEADER_SEND 2
#define MMSH_STATUS_ASF_HEADER_SEND 3
#define MMSH_STATUS_ASF_DATA_SENDING 4

#define ASF_STATUS_NULL 0
#define ASF_STATUS_SET_HEADER 1
#define ASF_HEADER_BUFSIZE 65535

#define PATHNAME_SIZE 2048

#define DEBUGMOD

#ifdef DEBUGMOD
#define DEBUG(fmt, ...) g_print(g_strdup_printf("%d: %s",__LINE__, fmt), __VA_ARGS__)
#define DEBUGLINE() g_print(g_strdup_printf("DEBUGLINE : %d\n",__LINE__))
#else
#define DEBUG(fmt, ...) g_print("")
#define DEBUGLINE() g_print("")
#endif


/* global value */
gboolean sigint_flag = FALSE;
GstClockTime latency = LATENCY;
gint polling_second = POLLING_SECOND;
gint stream_amount = STREAM_AMOUNT;
gint mms_base_port = MMS_BASE_PORT;
gint mms_ex_port = MMS_EX_PORT;
gint canvas_width = 3; // horizontal tiling amount

/* Structure to contain mms stream infomation. */
typedef struct _MmsData {
  GMainLoop  *loop;   // main loop
  guint number;       // stream number
  gint64 buffer_time; // current time
  clock_t clock;      // apprication time
  gulong prob_hd;     // probe hundler ID for mmssrc
  gulong prob_hd_v_eos;
  gulong prob_hd_a_eos;
  gboolean v_appflag, a_appflag;
  gchar *mms_location; // target url

  GstElement *pipeline;

  /* source elements */
  GstElement *source;
  GstElement *queue;
  GstElement *decoder;
  GstElement *v_queue;
  GstElement *colorspace;
  GstElement *scale;
  GstElement *rate;
  GstElement *filter;
  GstElement *videobox;
  /* audio source */
  GstElement *a_queue;
  GstElement *a_convert;
  GstElement *a_resample;
  GstElement *a_filter;
  /* caps */
  GstCaps    *filtercaps;
  GstCaps    *a_filtercaps;

  /* app */
  GstElement *v_appsink;
  GstElement *v_appsrc;
  GstElement *v_app_q;
  //GstElement *v_app_filter;
  GstElement *a_appsink;
  GstElement *a_appsrc;
  GstElement *a_app_q;
  //GstElement *a_app_filter;

  /* Parent Object (CustomData) */
  gpointer   parent;

  gint width, height;

} MmsData;

/* Structure to contain all our information, so we can pass it to callbacks */
typedef struct _CustomData {
  GMainLoop  *loop;     // main loop
  GstElement *pipeline; // main pipeline
  /* source elements */
  GstElement *source1, *queue1, *decoder1, *scale1, *rate1,  *filter1, *videobox1;
  /* video mixer */ 
  GstElement *mixer, *clrspace, *sink;
  /* audio source */
  GstElement *a_source ,*a_queue1, *a_convert1, *a_resample1, *a_filter1;
  /* audio mixer */
  GstElement *a_mixer, *a_sink;
  /* caps */
  GstCaps    *filtercaps1;
  GstCaps    *a_filtercaps1;
  /* etc */
  GstElement *frz1;
  /* for application */
  GstElement *encoder, *a_encoder, *asfmux, *appsink;

  /* mms source object */
  MmsData    **mms;

  /* mmsh server*/
  gint mmsh_status;
  SOCKET mmsh_socket;
  GMainLoop  *mmsh_loop;
  gint asf_status;
  char asf_head_buffer[ASF_HEADER_BUFSIZE];
  gint asf_head_size;
  gint packet_count;

} CustomData;

/** function prototype declare */
void sigcatch(int);
int httptest(char *);
static gboolean print_element_info(CustomData*);
static gboolean print_field (GQuark, const GValue *, gpointer);
static void print_caps (const GstCaps *, const gchar *);
static void print_pad_capabilities (GstElement *, gchar *);
void main_loop_quit_all(CustomData *);
static gboolean bus_call (GstBus *, GstMessage *, gpointer);
static gboolean bus_call_sub (GstBus *, GstMessage *, gpointer);
static void pad_added_handler1 (GstElement *, GstPad *, CustomData *);
static void mms_pad_added_handler (GstElement *, GstPad *, MmsData *);
static void cb_catch_buffer (GstPad *, GstBuffer *, gpointer);
void dispose_mms_stream(MmsData *);
gboolean mms_loop(MmsData *);
static void v_new_buffer (GstElement *, MmsData *);
static void a_new_buffer (GstElement *, MmsData *);
void noprint(const gchar *);
void print_to_printerr(const gchar *);
static void new_buffer (GstElement *, CustomData *);
static gboolean cb_print_position (GstElement *);
void notify(gpointer);
gpointer thread(gpointer);
static void cb_catch_v_eos (GstPad *, GstEvent *, gpointer);
static void cb_catch_a_eos (GstPad *, GstEvent *, gpointer);

/* sigint trap */
void sigcatch(int sig) {
  sigint_flag=TRUE;
  signal(SIGINT, sigcatch);
}
/* print debug info */
static gboolean print_element_info(CustomData *data){
  GstIterator *ite;
  GstElement *elem;
  gint i;
  gboolean done;

  if(sigint_flag){
    sigint_flag=FALSE;

    g_print ("received SIGINT: print all elements' information. \n");
    g_print ("<<<<parent pipeline>>>> \n");
    g_print("pipeline %s states: %d\n", GST_ELEMENT_NAME(data->pipeline), GST_STATE(data->pipeline));

    ite = gst_bin_iterate_elements(GST_BIN(data->pipeline));
    done = FALSE;
    while (!done) {
      switch (gst_iterator_next (ite, (GValue *)&elem)) {
      case GST_ITERATOR_OK:

        g_print("%s states: %d\n", GST_ELEMENT_NAME(elem), GST_STATE(elem));
          DEBUG("base timestamp: %llu  \n", gst_element_get_base_time(elem));
          DEBUG("start timestamp: %llu  \n", gst_element_get_start_time(elem));

        break;
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (ite);
        break;
      case GST_ITERATOR_ERROR:
        done = TRUE;
        break;
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
      }
    }
    gst_iterator_free (ite);

    g_print ("<<<<child pipeline>>>> \n");

    for ( i=0 ; i < stream_amount ; i++ ){
      g_print("pipeline %s states: %d\n", GST_ELEMENT_NAME(data->mms[i]->pipeline), GST_STATE(data->mms[i]->pipeline));
      ite = gst_bin_iterate_elements(GST_BIN(data->mms[i]->pipeline));
      done = FALSE;
      while (!done) {
        switch (gst_iterator_next (ite, (GValue *)&elem)) {
        case GST_ITERATOR_OK:

          g_print("%s states: %d\n", GST_ELEMENT_NAME(elem), GST_STATE(elem));
          DEBUG("base timestamp: %llu  \n", gst_element_get_base_time(elem));
          DEBUG("start timestamp: %llu  \n", gst_element_get_start_time(elem));

          break;
        case GST_ITERATOR_RESYNC:
          gst_iterator_resync (ite);
          break;
        case GST_ITERATOR_ERROR:
          done = TRUE;
          break;
        case GST_ITERATOR_DONE:
          done = TRUE;
          break;
        }
      }
      gst_iterator_free (ite);
    }

    /* write debug info */
    print_pad_capabilities (data->sink,     "sink" );
    print_pad_capabilities (data->mms[0]->v_appsink,     "sink" );
  }

  return TRUE;

}

/* Functions below print the Capabilities in a human-friendly format */
static gboolean print_field (GQuark field, const GValue * value, gpointer pfx) {
  gchar *str = gst_value_serialize (value);
  g_print ("%s  %15s: %s\n", (gchar *) pfx, g_quark_to_string (field), str);
  g_free (str);
  return TRUE;
}

static void print_caps (const GstCaps * caps, const gchar * pfx) {
  guint i;

  g_return_if_fail (caps != NULL);

  if (gst_caps_is_any (caps)) {
    g_print ("%sANY\n", pfx);
    return;
  }
  if (gst_caps_is_empty (caps)) {
    g_print ("%sEMPTY\n", pfx);
    return;
  }

  for (i = 0; i < gst_caps_get_size (caps); i++) {
    GstStructure *structure = gst_caps_get_structure (caps, i);

    g_print ("%s%s\n", pfx, gst_structure_get_name (structure));
    gst_structure_foreach (structure, print_field, (gpointer) pfx);
  }
}

/* Shows the CURRENT capabilities of the requested pad in the given element */
static void print_pad_capabilities (GstElement *element, gchar *pad_name) {
  GstPad *pad = NULL;
  GstCaps *caps = NULL;

  /* Retrieve pad */
  pad = gst_element_get_static_pad (element, pad_name);
  if (!pad) {
    g_printerr ("Could not retrieve pad '%s' of %s\n", pad_name, GST_OBJECT_NAME(element));
    return;
  }

  /* Retrieve negotiated caps (or acceptable caps if negotiation is not finished yet) */
  caps = gst_pad_get_current_caps (pad);
  if (!caps){

    g_print ("No Negotiated Caps for the %s pad of %s:\n", pad_name, GST_OBJECT_NAME(element));
    //caps = gst_pad_get_caps_reffed (pad);
    caps = gst_pad_query_caps (pad, NULL);
    print_caps (caps, "      ");
    gst_caps_unref (caps);

  }else{
    /* Print and free */
    g_print ("Caps for the %s pad of %s:\n", pad_name, GST_OBJECT_NAME(element));
    print_caps (caps, "      ");
    gst_caps_unref (caps);
  }

  gst_object_unref (pad);
}

/* quit all generated loops */
void main_loop_quit_all(CustomData *data){
  gint i;
  for(i=0;i<stream_amount;i++){
    g_main_loop_quit (data->mms[i]->loop);
  }
  g_main_loop_quit(data->loop);
}

/* bus message from pipeline */
static gboolean
  bus_call (
  GstBus     *bus,
  GstMessage *msg,
  gpointer   gdata)
{
  CustomData *data = (CustomData *)gdata;
  GMainLoop *loop = data->loop;
  gchar **str;
  MmsData *mmsdata;
  guint64 number;
  gpointer a,b;

  switch (GST_MESSAGE_TYPE (msg)) {
  case GST_MESSAGE_ELEMENT:
    DEBUG ("Element message received at parent pipeline\n");
    /* unlink mms stream elements */
    str = g_strsplit(GST_OBJECT_NAME (msg->src), "__", 2);
    if (str[1] != NULL){
      /* parse number */
      number = g_ascii_strtoull(str[1], NULL, 10);
      g_print("Caught EOS event at %s on #%d\n", GST_OBJECT_NAME (msg->src), number);
      mmsdata = data->mms[number];

      /* dispose */
      a=(void *)msg->src;
      b=(void *)mmsdata->a_app_q;
      if(a==b){
        gst_element_set_state (mmsdata->a_appsrc, GST_STATE_NULL);
        gst_element_set_state (mmsdata->a_app_q, GST_STATE_NULL);
        gst_bin_remove_many (
          GST_BIN (data->pipeline), 
          mmsdata->a_appsrc, mmsdata->a_app_q,
          NULL);
        mmsdata->prob_hd_a_eos = 0;
      }else if(a == (void *)mmsdata->v_app_q){
        gst_element_set_state (mmsdata->v_appsrc, GST_STATE_NULL);
        gst_element_set_state (mmsdata->v_app_q, GST_STATE_NULL);
        gst_bin_remove_many (
          GST_BIN (data->pipeline), 
          mmsdata->v_appsrc, mmsdata->v_app_q,
          NULL);
        mmsdata->prob_hd_v_eos = 0;
      }

      break;
    }
    break;
  case GST_MESSAGE_EOS:
    DEBUG ("End of stream at parent pipeline\n");
    /* unlink mms stream elements */
    str = g_strsplit(GST_OBJECT_NAME (msg->src), "__", 2);
    if (str[1] != NULL){
      /* parse number */
      number = g_ascii_strtoull(str[1], NULL, 10);
      g_print("Caught EOS event at %s on #%d\n", GST_OBJECT_NAME (msg->src), number);
      mmsdata = data->mms[number];

      /* dispose */
      a=(void *)msg->src;
      b=(void *)mmsdata->a_app_q;
      if(a==b){
        gst_element_set_state (mmsdata->a_appsrc, GST_STATE_NULL);
        gst_element_set_state (mmsdata->a_app_q, GST_STATE_NULL);
        gst_bin_remove_many (
          GST_BIN (data->pipeline), 
          mmsdata->a_appsrc, mmsdata->a_app_q,
          NULL);
        mmsdata->prob_hd_a_eos = 0;
      }else if(a == (void *)mmsdata->v_app_q){
        gst_element_set_state (mmsdata->v_appsrc, GST_STATE_NULL);
        gst_element_set_state (mmsdata->v_app_q, GST_STATE_NULL);
        gst_bin_remove_many (
          GST_BIN (data->pipeline), 
          mmsdata->v_appsrc, mmsdata->v_app_q,
          NULL);
        mmsdata->prob_hd_v_eos = 0;
      }

      break;
    }

    main_loop_quit_all(data);
    break;

  case GST_MESSAGE_ERROR: {
    gchar  *debug;
    GError *error;
    DEBUG ("Message received. Type:%s from %s\n", GST_MESSAGE_TYPE_NAME(msg), GST_OBJECT_NAME (msg->src));
    gst_message_parse_error (msg, &error, &debug);
    g_printerr ("Error received from element %s: %s\n", GST_OBJECT_NAME (msg->src), error->message);
    g_printerr ("Debugging information: %s\n", debug ? debug : "none");

    /* unlink mms stream elements */
    str = g_strsplit(GST_OBJECT_NAME (msg->src), "__", 2);
    if (str[1] != NULL){
      /* parse number */
      number = g_ascii_strtoull(str[1], NULL, 10);
      mmsdata = data->mms[number];
      /* remove from bin */
      if(mmsdata){
        /* dispose */ 
        dispose_mms_stream(mmsdata);

      }else{
        g_printerr("mms stream #%d isn't find.\n", number);
      }

      g_error_free (error);
      g_free (debug);
      break;
    }

    // if parent stream error coused , close process
    g_error_free (error);
    g_free (debug);

    main_loop_quit_all(data);
    break;
                          }
  case GST_MESSAGE_WARNING: {
    gchar  *debug;
    GError *error;
    DEBUG ("Message received. Type:%s from %s\n", GST_MESSAGE_TYPE_NAME(msg), GST_OBJECT_NAME (msg->src));
    gst_message_parse_warning (msg, &error, &debug);
    g_printerr ("Warning received from element %s: %s\n", GST_OBJECT_NAME (msg->src), error->message);
    g_printerr ("Debugging information: %s\n", debug ? debug : "none");
    break;
                            }
  case GST_MESSAGE_STATE_CHANGED:
    /* We are only interested in state-changed messages from the pipeline */
    {
      GstState old_state, new_state, pending_state;
      time_t timer;
      time(&timer);
      DEBUGLINE();
      gst_message_parse_state_changed (msg, &old_state, &new_state, &pending_state);
      DEBUGLINE();
      g_print ("Element %s state changed from %s to %s:\n",
        GST_OBJECT_NAME (msg->src), gst_element_state_get_name (old_state), gst_element_state_get_name (new_state));
      DEBUGLINE();
    }
    break;
  case GST_MESSAGE_NEW_CLOCK:
    DEBUG ("New Clock Created\n");
    break;
  case GST_MESSAGE_CLOCK_LOST:
    DEBUG ("Clock Lost\n");
    break;
  case GST_MESSAGE_LATENCY:
    DEBUG ("Pipeline required latency.\n");
    break;
  case GST_MESSAGE_STREAM_STATUS: {
    GstStreamStatusType type;
    GstElement *owner;
    gst_message_parse_stream_status (msg, &type, &owner);
    DEBUG ("Stream_status received from element %s type: %d owner:%s\n", GST_OBJECT_NAME (msg->src), type, GST_OBJECT_NAME (owner));
    break;
                                  }
  default:
    DEBUG ("Message received. Type:%s from %s\n", GST_MESSAGE_TYPE_NAME(msg), GST_OBJECT_NAME (msg->src));
    break;
  }

  return TRUE;
}

/* bus message from mms pipeline */
static gboolean
  bus_call_sub (GstBus *bus,
  GstMessage *msg,
  gpointer   gdata)
{
  MmsData *mmsdata = (MmsData *)gdata;
  GMainLoop *loop = mmsdata->loop;
  gchar **str;
  guint64 number;
 // GstPad *pad;
  CustomData *parent = (CustomData *) (mmsdata->parent);

  switch (GST_MESSAGE_TYPE (msg)) {

  case GST_MESSAGE_EOS:
    DEBUG ("End of stream at sub stream pipeline\n");
    /* unlink mms stream elements */
    DEBUG ("GST_OBJECT_NAME (msg->src): %s\n", GST_OBJECT_NAME (msg->src));
    str = g_strsplit(GST_OBJECT_NAME (msg->src), "__", 2);
    
    if (str[1] != NULL){
      /* parse number */
      number = g_ascii_strtoull(str[1], NULL, 10);
      DEBUG("Caught EOS event at %s on #%d\n", GST_OBJECT_NAME (msg->src), number);

      /* dispose */ 
      dispose_mms_stream(mmsdata);
      break;
    }
    break;

  case GST_MESSAGE_ERROR: {
    gchar  *debug;
    GError *error;
    DEBUG ("Message received. Type:%s from %s\n", GST_MESSAGE_TYPE_NAME(msg), GST_OBJECT_NAME (msg->src));
    gst_message_parse_error (msg, &error, &debug);
    g_printerr ("Error received from element %s: %s\n", GST_OBJECT_NAME (msg->src), error->message);
    g_printerr ("Debugging information: %s\n", debug ? debug : "none");
    /* dispose */ 
    dispose_mms_stream(mmsdata);

    break;
                          }
  case GST_MESSAGE_WARNING: {
    gchar  *debug;
    GError *error;
    DEBUG ("Message received. Type:%s from %s\n", GST_MESSAGE_TYPE_NAME(msg), GST_OBJECT_NAME (msg->src));
    gst_message_parse_warning (msg, &error, &debug);
    g_printerr ("Warning received from element %s: %s\n", GST_OBJECT_NAME (msg->src), error->message);
    g_printerr ("Debugging information: %s\n", debug ? debug : "none");
    break;
                            }
  case GST_MESSAGE_STATE_CHANGED:
    /* We are only interested in state-changed messages from the pipeline */
    {
      GstState old_state, new_state, pending_state;
      time_t timer;
      time(&timer);
      gst_message_parse_state_changed (msg, &old_state, &new_state, &pending_state);

      g_print ("Element %s state changed from %s to %s:\n",
        GST_OBJECT_NAME (msg->src), gst_element_state_get_name (old_state), gst_element_state_get_name (new_state));DEBUGLINE();
    }
    break;
  case GST_MESSAGE_NEW_CLOCK:
    DEBUG ("New Clock Created\n");DEBUGLINE();
    break;
  case GST_MESSAGE_CLOCK_LOST:
    DEBUG ("Clock Lost\n");DEBUGLINE();
    break;
  case GST_MESSAGE_LATENCY:
    DEBUG ("Pipeline required latency.\n");DEBUGLINE();
    break;
  case GST_MESSAGE_STREAM_STATUS: {
    GstStreamStatusType type;
    GstElement *owner;
    gst_message_parse_stream_status (msg, &type, &owner);
    DEBUG ("Stream_status received from element %s type: %d owner:%s\n", GST_OBJECT_NAME (msg->src), type, GST_OBJECT_NAME (owner));DEBUGLINE();
    break;
                                  }
  default:
    DEBUG ("Message received. Type:%s from %s\n", GST_MESSAGE_TYPE_NAME(msg), GST_OBJECT_NAME (msg->src));DEBUGLINE();
    break;
  }

  return TRUE;
}

/* This function will be called by the pad-added signal */
static void pad_added_handler1 (GstElement *src, GstPad *new_pad, CustomData *data) {
  GstPad *vsink_pad      = gst_element_get_static_pad (data->scale1, "sink");
  GstCaps *sink_pad_caps = NULL;

  GstPadLinkReturn ret;
  GstCaps *new_pad_caps        = NULL;
  GstStructure *new_pad_struct = NULL;
  const gchar *new_pad_type    = NULL;

  g_print ("Received new pad '%s' from '%s':\n", GST_PAD_NAME (new_pad), GST_ELEMENT_NAME (src));
  DEBUGLINE();
  /* Check the new pad's type */
  //new_pad_caps   = gst_pad_get_caps (new_pad);
  new_pad_caps   = gst_pad_query_caps (new_pad, NULL);
  new_pad_struct = gst_caps_get_structure (new_pad_caps, 0);
  new_pad_type   = gst_structure_get_name (new_pad_struct);
  DEBUGLINE();
  //sink_pad_caps = gst_pad_get_caps (vsink_pad);
  sink_pad_caps = gst_pad_query_caps (vsink_pad, NULL);
  DEBUGLINE();
  /* Attempt the link */
  if (g_str_has_prefix (new_pad_type, "audio/x-raw")) {
    goto exig;
  } else {
    ret = gst_pad_link (new_pad, vsink_pad);
  }
  DEBUGLINE();
  if (GST_PAD_LINK_FAILED (ret)) {
    g_print ("  Type is '%s' but link failed.\n", new_pad_type);
  } else {
    g_print ("  Link succeeded (type '%s').\n", new_pad_type);
  }DEBUGLINE();

exig:
  /* Unreference the new pad's caps, if we got them */
  if (new_pad_caps != NULL)
    gst_caps_unref (new_pad_caps);
  DEBUGLINE();
  if (sink_pad_caps != NULL)
    gst_caps_unref (sink_pad_caps);
  DEBUGLINE();
  /* Unreference the sink pad */
  gst_object_unref (vsink_pad);
}

/* This function will be called by the pad-added signal */ /* for mms stream */
static void mms_pad_added_handler (GstElement *src, GstPad *new_pad, MmsData *data) {
  GstPad *vsink_pad;
  GstPad *asink_pad;

  GstPadLinkReturn ret;
  GstCaps *new_pad_caps        = NULL;
  GstStructure *new_pad_struct = NULL;
  const gchar *new_pad_type    = NULL;

  g_print ("Received new pad '%s' from '%s':\n", GST_PAD_NAME (new_pad), GST_ELEMENT_NAME (src));

  /* Check the new pad's type */
  //new_pad_caps   = gst_pad_get_caps (new_pad);
  new_pad_caps   = gst_pad_query_caps (new_pad, NULL);
  new_pad_struct = gst_caps_get_structure (new_pad_caps, 0);
  new_pad_type   = gst_structure_get_name (new_pad_struct);

  /* Attempt the link */
  if (g_str_has_prefix (new_pad_type, "audio/x-raw")) {
    gst_bin_add_many (GST_BIN (data->pipeline), 
      data->a_queue, data->a_convert, data->a_resample, data->a_appsink,
      NULL);
    gst_element_link_many (data->a_queue, data->a_convert, data->a_resample, data->a_appsink, NULL);
    asink_pad = gst_element_get_static_pad (data->a_queue,   "sink");
    ret = gst_pad_link (new_pad, asink_pad);
    DEBUGLINE();
    gst_element_set_state (data->a_appsink,  GST_STATE_PLAYING);
    gst_element_set_state (data->a_resample, GST_STATE_PLAYING);
    gst_element_set_state (data->a_convert,  GST_STATE_PLAYING);
    gst_element_set_state (data->a_queue,    GST_STATE_PLAYING);
    DEBUGLINE();
    gst_object_unref (asink_pad);

  } else if (g_str_has_prefix (new_pad_type, "video")){
    gst_bin_add_many (GST_BIN (data->pipeline), 
      data->v_queue, data->colorspace, data->scale, data->rate, data->filter, data->videobox, data->v_appsink,
      NULL);
    gst_element_link_many (data->v_queue, data->colorspace, data->scale, data->rate, data->filter,  data->videobox,  data->v_appsink, NULL);
    vsink_pad = gst_element_get_static_pad (data->v_queue,   "sink");
    ret = gst_pad_link (new_pad, vsink_pad);
    DEBUGLINE();
    gst_element_set_state (data->v_appsink,GST_STATE_PLAYING);
    gst_element_set_state (data->videobox, GST_STATE_PLAYING);
    gst_element_set_state (data->filter,   GST_STATE_PLAYING);
    gst_element_set_state (data->rate,     GST_STATE_PLAYING);
    gst_element_set_state (data->scale,    GST_STATE_PLAYING);
    gst_element_set_state (data->colorspace,    GST_STATE_PLAYING);
    gst_element_set_state (data->v_queue,  GST_STATE_PLAYING);
    DEBUGLINE();
    gst_object_unref (vsink_pad);
  }

  if (GST_PAD_LINK_FAILED (ret)) {
    g_print ("  Type is '%s' but link failed.\n", new_pad_type);
  } else {
    g_print ("  Link succeeded (type '%s').\n", new_pad_type);
  }
  DEBUGLINE();
  /* Unreference the new pad's caps, if we got them */
  if (new_pad_caps != NULL)
    gst_caps_unref (new_pad_caps);
}

static void
  cb_catch_buffer (GstPad    *pad,
  GstBuffer *buffer,
  gpointer user_data)
{
  gint64     pos;
  MmsData    *mmsdata = (MmsData *)user_data;
  CustomData *data    = (CustomData *)mmsdata->parent;
  GstFormat   format  = GST_FORMAT_TIME;
  DEBUGLINE();
  if (gst_element_query_position (GST_ELEMENT_CAST(data->pipeline), format, &pos)) {
    mmsdata->buffer_time = pos; // pipeline running time
  }
  mmsdata->clock = clock(); // apprication running time
  //g_print("buffer passed through!\n");
}

/* video appsrc has sweeped */
static void cb_catch_v_eos (
  GstPad   *pad,
  GstEvent *event,
  gpointer user_data)
{
  MmsData    *mmsdata = (MmsData *)user_data;
  CustomData *parent    = (CustomData *)mmsdata->parent;

  /* if eos-event*/
  if(GST_EVENT_TYPE(event) == GST_EVENT_EOS){
    GstMessage *message;
    GstBus *bus;
    //gst_pad_remove_event_probe (pad, mmsdata->prob_hd_v_eos);
    gst_pad_remove_probe (pad, mmsdata->prob_hd_v_eos);
    // send message to pipeline
    message = gst_message_new_element(GST_OBJECT(mmsdata->v_app_q), NULL);
    bus = gst_pipeline_get_bus (GST_PIPELINE (parent->pipeline));DEBUGLINE();
    gst_bus_post (bus, message);
    gst_object_unref (bus);
  }
}

/* audio appsrc has sweeped */
static void cb_catch_a_eos (
  GstPad    *pad,
  GstEvent *event,
  gpointer user_data)
{
  MmsData    *mmsdata = (MmsData *)user_data;
  CustomData *parent    = (CustomData *)mmsdata->parent;
  DEBUGLINE();
  /* if eos-event*/
  if(GST_EVENT_TYPE(event) == GST_EVENT_EOS){
    GstMessage *message;
    GstBus *bus;
    //gst_pad_remove_event_probe (pad, mmsdata->prob_hd_a_eos);
    gst_pad_remove_probe (pad, mmsdata->prob_hd_a_eos);
    // send message to pipeline
    message = gst_message_new_element(GST_OBJECT(mmsdata->a_app_q) ,NULL);
    bus = gst_pipeline_get_bus (GST_PIPELINE (parent->pipeline));
    gst_bus_post (bus, message);
    gst_object_unref (bus);DEBUGLINE();
  }
}

/* Dispose mms stream and appsrc injection */
void
  dispose_mms_stream(MmsData *mmsdata){
    GstPad     *pad;
    CustomData *parent = (CustomData *) (mmsdata->parent);
    GstFlowReturn ret;
    //gboolean ret;

    DEBUG ("Dispose mms stream on #%d stream...\n", mmsdata->number);

    pad = gst_element_get_static_pad (mmsdata->source, "src");
    if (pad == NULL) DEBUG ("fail to get pad.\n");
    //gst_pad_remove_buffer_probe (pad, mmsdata->prob_hd);
    gst_pad_remove_probe (pad, mmsdata->prob_hd);
    mmsdata->prob_hd = 0;
    gst_object_unref (pad);
    gst_element_set_state (mmsdata->pipeline,   GST_STATE_NULL);
    DEBUGLINE();
    // main
    gst_bin_remove_many (GST_BIN (mmsdata->pipeline), 
      mmsdata->source, mmsdata->queue, mmsdata->decoder, 
      NULL);
    DEBUGLINE();
    // video
    if (gst_bin_get_by_name(GST_BIN(mmsdata->pipeline), GST_ELEMENT_NAME(mmsdata->v_appsink)) != NULL){
      gst_bin_remove_many (GST_BIN (mmsdata->pipeline), 
        mmsdata->v_queue, mmsdata->scale, mmsdata->rate, mmsdata->filter, mmsdata->videobox, mmsdata->v_appsink,
        NULL);
    }
    DEBUGLINE();
    // audio
    if (gst_bin_get_by_name(GST_BIN(mmsdata->pipeline), GST_ELEMENT_NAME(mmsdata->a_appsink)) != NULL){
      gst_bin_remove_many (GST_BIN (mmsdata->pipeline), 
        mmsdata->a_queue, mmsdata->a_convert, mmsdata->a_resample, mmsdata->a_appsink,
        NULL);
    }
    DEBUGLINE();
    mmsdata->buffer_time = 0;
    mmsdata->clock = clock();

    /* dispose appsrc */
    DEBUGLINE();
    // video
    if (gst_bin_get_by_name(GST_BIN(parent->pipeline), GST_ELEMENT_NAME(mmsdata->v_appsrc)) != NULL){
      g_signal_emit_by_name (mmsdata->v_appsrc, "end-of-stream", &ret);
    }
    DEBUGLINE();
    // audio
    if (gst_bin_get_by_name(GST_BIN(parent->pipeline), GST_ELEMENT_NAME(mmsdata->a_appsrc)) != NULL){
      g_signal_emit_by_name (mmsdata->a_appsrc, "end-of-stream", &ret);
    }

    DEBUG ("Disposed #%d stream...\n", mmsdata->number);
}


gboolean
  mms_loop(MmsData *mmsdata){
    CustomData *parent = (CustomData *) (mmsdata->parent);
    guint i = mmsdata->number;
    GstPad *pad;
    int status;

    //DEBUG ("Polling for #%d stream...\n", i);
    DEBUGLINE();
    /* timeout check */
    /* If buffer idle time > timeout_length then despose mms stream */
    if (gst_bin_get_by_name(GST_BIN(mmsdata->pipeline), GST_ELEMENT_NAME(mmsdata->source)) != NULL
      ) {
        gint64 c_pos; //current position
        GstFormat format = GST_FORMAT_TIME;
        clock_t c_clock = clock();
        DEBUGLINE();
        gst_element_query_position (parent->pipeline, format, &c_pos); // current position
        DEBUGLINE();
        /* If state isn't PLAYING,  */
        if(GST_STATE(mmsdata->source) == GST_STATE_PLAYING &&
          mmsdata->buffer_time > 0 &&
          c_pos - mmsdata->buffer_time > BUFFER_TIMEOUT
          ){
            /* dispose */ 
            g_printerr ("Timed out: perhaps no more buffer exists. \n");
            dispose_mms_stream(mmsdata);
DEBUGLINE();
        } 
        /* Force timeout */
        else if( (c_clock - mmsdata->clock) / CLOCKS_PER_SEC > CLOCK_TIMEOUT
          ){
            /* dispose */ 
            g_printerr ("Timed out: stream has stopped over timeout range. \n");
DEBUGLINE();
            dispose_mms_stream(mmsdata);
        }
    }

    /* IF the pipeline already has this mms source, there's no porocess to do */
    if(gst_bin_get_by_name(GST_BIN(mmsdata->pipeline), GST_ELEMENT_NAME(mmsdata->source)) == NULL &&
      GST_STATE(mmsdata->source) <= 1){ // NULL
        GstBus *bus;
        DEBUGLINE();
        /* http status check */
        status = httptest(g_strdup_printf("http://%s", mmsdata->mms_location));
        
        //status=-1;
        DEBUGLINE();
        if( status < 0){
          DEBUG("no #%d mms stream yet\n", mmsdata->number);
          return TRUE;
        }
        DEBUGLINE();
        if( status == HTTP_BUSY){
          DEBUG("mms stream #%d has already connected\n", mmsdata->number);
          return TRUE;
        }
        DEBUGLINE();
        /* Add elements to mms-pipeline */
        gst_bin_add_many (GST_BIN (mmsdata->pipeline), 
          mmsdata->source, mmsdata->queue, mmsdata->decoder, 
          NULL);

        /* we link the elements together */
        gst_element_link_many (mmsdata->source, mmsdata->queue, mmsdata->decoder, NULL);

        /* Set the pipeline to "playing" state. */

        bus = gst_pipeline_get_bus (GST_PIPELINE (mmsdata->pipeline));
        gst_bus_add_watch (bus, bus_call_sub, mmsdata);
        gst_object_unref (bus);
        DEBUGLINE();
        /* capture the buffer */
        pad = gst_element_get_static_pad (mmsdata->source, "src");
        //mmsdata->prob_hd = gst_pad_add_buffer_probe (pad, (GCallback) cb_catch_buffer, mmsdata);
        mmsdata->prob_hd = gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_BUFFER, (GstPadProbeCallback) cb_catch_buffer, mmsdata, NULL);
        gst_object_unref (pad);
        DEBUGLINE();
        /* set clock*/
        mmsdata->clock = clock();
        DEBUGLINE();
        /* set latency */
        if (!gst_element_send_event (mmsdata->pipeline, gst_event_new_latency (latency)))
        	  g_warning ("latency set failed\n");

        gst_element_set_state (mmsdata->pipeline, GST_STATE_PLAYING);
        DEBUGLINE();
    }
    DEBUGLINE();
    return TRUE;
}

/* The appsink has received a buffer */
void v_new_buffer (GstElement *sink, MmsData *mmsdata) {
  GstBuffer *buffer;
  GstFlowReturn ret;

  GstPadTemplate *pad_template;
  GstPad *mixer_pad;
  GstPad *videobox_pad;
  GstPad *pad;

  CustomData *parent = (CustomData *)(mmsdata->parent);
  int i=mmsdata->number;

  if(gst_bin_get_by_name(GST_BIN(parent->pipeline), GST_ELEMENT_NAME(mmsdata->v_appsrc)) == NULL &&
    GST_STATE(mmsdata->v_appsrc) <= 1 && mmsdata->prob_hd_v_eos == 0){

      gst_bin_add_many (GST_BIN (parent->pipeline), 
        mmsdata->v_appsrc, mmsdata->v_app_q,
        NULL);
      DEBUGLINE();
      gst_element_link_many (mmsdata->v_appsrc, mmsdata->v_app_q, NULL);
      DEBUGLINE();
      /* Manually link the Element, which has "Request" pads */
      /* video stream */
      pad_template   = gst_element_class_get_pad_template (GST_ELEMENT_GET_CLASS (parent->mixer), "sink_%d");
      mixer_pad      = gst_element_request_pad (parent->mixer, pad_template, NULL, NULL);
      g_print ("Obtained request pad %s for video mixer.\n", gst_pad_get_name (mixer_pad));
      g_object_set (G_OBJECT (mixer_pad), "xpos", mmsdata->width * (i % canvas_width), "ypos", 
        mmsdata->height * (i / canvas_width), NULL);
      videobox_pad   = gst_element_get_static_pad (mmsdata->v_app_q, "src");

      if (gst_pad_link (videobox_pad, mixer_pad) != GST_PAD_LINK_OK) {
        g_printerr ("Cannot be linked.\n");
      }
      gst_object_unref (videobox_pad);
      gst_object_unref (pad_template);
      gst_object_unref (mixer_pad);
      DEBUGLINE();
      /* install new probe for EOS */
      pad = gst_element_get_static_pad (mmsdata->v_app_q, "src");
      //mmsdata->prob_hd_v_eos = gst_pad_add_event_probe (pad, G_CALLBACK(cb_catch_v_eos), mmsdata);
      mmsdata->prob_hd_a_eos = gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
                                                  (GstPadProbeCallback)cb_catch_v_eos, mmsdata, NULL);
      gst_object_unref (pad);
      DEBUGLINE();
      gst_element_set_state (mmsdata->v_appsrc, GST_STATE_PLAYING);
      gst_element_set_state (mmsdata->v_app_q, GST_STATE_PLAYING);

  }else if (GST_STATE(mmsdata->v_appsrc) == GST_STATE_PLAYING){  
    //gint64 pos;
    GstFormat format = GST_FORMAT_TIME;
    g_signal_emit_by_name (sink, "pull-sample", &buffer);
    DEBUGLINE();
    g_signal_emit_by_name (mmsdata->v_appsrc, "push-buffer", buffer, &ret);
    gst_buffer_unref (buffer);
  }
}
void a_new_buffer (GstElement *sink, MmsData *mmsdata) {
  GstBuffer *buffer;
  GstFlowReturn ret;

  GstPadTemplate *pad_template;
  GstPad *mixer_pad;
  GstPad *audio_pad;
  GstPad *pad;
  CustomData *parent = (CustomData *)(mmsdata->parent);
  int i=mmsdata->number;
  
  if(gst_bin_get_by_name(GST_BIN(parent->pipeline), GST_ELEMENT_NAME(mmsdata->a_appsrc)) == NULL &&
    GST_STATE(mmsdata->a_appsrc) <= 1 && mmsdata->prob_hd_a_eos == 0){

      gst_bin_add_many (GST_BIN (parent->pipeline), 
        mmsdata->a_appsrc, mmsdata->a_app_q,
        NULL);
      DEBUGLINE();
      gst_element_link_many (mmsdata->a_appsrc, mmsdata->a_app_q, NULL);
      /* Manually link the Element, which has "Request" pads */
      DEBUGLINE();
      /* audio stream */
      pad_template   = gst_element_class_get_pad_template (GST_ELEMENT_GET_CLASS (parent->a_mixer), "sink%d");
      mixer_pad      = gst_element_request_pad (parent->a_mixer, pad_template, NULL, NULL);
      g_print ("Obtained request pad %s for audio mixer.\n", gst_pad_get_name (mixer_pad));
      audio_pad      = gst_element_get_static_pad (mmsdata->a_app_q, "src");
      if (gst_pad_link   (audio_pad, mixer_pad) != GST_PAD_LINK_OK) {
        g_printerr       ("Cannot be linked.\n");
      }
      gst_object_unref (audio_pad); 
      gst_object_unref (pad_template);
      gst_object_unref (mixer_pad);
      DEBUGLINE();
      /* install new probe for EOS */
      pad = gst_element_get_static_pad (mmsdata->a_app_q, "src");
      //mmsdata->prob_hd_a_eos = gst_pad_add_event_probe (pad, G_CALLBACK(cb_catch_a_eos), mmsdata);
      mmsdata->prob_hd_a_eos = gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
                                                  (GstPadProbeCallback)cb_catch_a_eos, mmsdata, NULL);
      gst_object_unref (pad);
      DEBUGLINE();
      gst_element_set_state (mmsdata->a_appsrc, GST_STATE_PLAYING);
      gst_element_set_state (mmsdata->a_app_q, GST_STATE_PLAYING);
      DEBUGLINE();
  }else if (GST_STATE(mmsdata->a_appsrc) == GST_STATE_PLAYING){  
    //gint64 pos;
    GstFormat format = GST_FORMAT_TIME;
    g_signal_emit_by_name (sink, "pull-sample", &buffer);
    g_signal_emit_by_name (mmsdata->a_appsrc, "push-buffer", buffer, &ret);
    DEBUGLINE();
    gst_buffer_unref (buffer);
  }
}

/* silent printer */
void noprint(const gchar *format)
{/* do nothing... */}

/* print to printerr wrapper */
void print_to_printerr(const gchar *format)
{g_printerr(format);}

/* The appsink has received a buffer */
static void new_buffer (GstElement *sink, CustomData *data) {
  GstSample *sample;
  GstBuffer *buffer;
  guint size;
  guint8 *bdata;
  GstMemory *memory;
  GstMapInfo info;
  DEBUGLINE();
    if (0 > _setmode( _fileno(stdout), _O_BINARY))
    g_printerr("NG\n");
    DEBUGLINE();
  /* Retrieve the buffer */
  g_signal_emit_by_name (sink, "pull-sample", &sample, NULL);
  if (sample) {
    buffer = gst_sample_get_buffer(sample);
    memory = gst_buffer_get_memory (buffer, 0);
    gst_memory_map (memory, &info, GST_MAP_READ);
    size  = info.size;
    bdata = info.data;

    /* set asf header */
    if (size > 0 && data != NULL && data->asf_status == ASF_STATUS_NULL){
      gchar *dest;
      //todo: calcurate mean bitrate
      char bitrate_property[38] = {0xCE, 0x75, 0xF8, 0x7B, 0x8D, 0x46, 0xD1, 0x11,
        0x8D, 0x82, 0x00, 0x60, 0x97, 0xC9, 0xA2, 0xB2,
        0x26, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x02, 0x00, 0x01, 0x00, 0x08, 0x0C, 0x04, 0x00,
        0x02, 0x00, 0x10, 0x89, 0x00, 0x00};
      size += 8;
      data->asf_head_buffer[0]='$';
      data->asf_head_buffer[1]='H';
      //data->asf_head_buffer[2]=(char)size;
      //data->asf_head_buffer[3]=(char)((size >> 8) & 0xFF);
      data->asf_head_buffer[9]=0x0C;
      //data->asf_head_buffer[10]=data->asf_head_buffer[2];
      //data->asf_head_buffer[11]=data->asf_head_buffer[3];
      dest = &(data->asf_head_buffer[12]);
      memcpy (dest, (gchar *)bdata, size - 8);

      /* add stream_bitrate_propaty */
      dest=(char *)malloc(50);
      memset(dest,0x00,50);
      memcpy(dest, bdata + (size - 8 - 50), 50);
      memcpy(data->asf_head_buffer + (size + 4 - 50), bitrate_property, 38);
      memcpy(data->asf_head_buffer + (size + 4 - 50 + 38), dest, 50);
      free(dest);
      DEBUGLINE();
      data->asf_head_buffer[2]=(char)(size+38);
      data->asf_head_buffer[3]=(char)(((size+38) >> 8) & 0xFF);
      data->asf_head_buffer[10]=data->asf_head_buffer[2];
      data->asf_head_buffer[11]=data->asf_head_buffer[3];
      data->asf_head_buffer[36] = data->asf_head_buffer[36] + 1; // headerobjectcount
      DEBUGLINE();
      data->asf_head_size = size+38+4;
      data->asf_status = ASF_STATUS_SET_HEADER;
      gst_buffer_unref (buffer);
      gst_sample_unref (sample);
      return;
    }

    /* print the buffer to socket */
    if (size > 0 && data != NULL  &&  data->mmsh_socket != (SOCKET)NULL &&
      (data->mmsh_status == MMSH_STATUS_ASF_HEADER_SEND || data->mmsh_status == MMSH_STATUS_ASF_DATA_SENDING)) {
        gchar *dest;
        size += 8;
        dest = (gchar *)malloc(size+4);
        memset(dest,0x00,size+4);
        dest[0]='$';
        dest[1]='D';
        dest[2]=(char)size;
        dest[3]=(char)((size >> 8) & 0xFF);
        dest[4]=(char)data->packet_count;
        dest[5]=(char)((data->packet_count >> 8) & 0xFF);
        dest[6]=(char)((data->packet_count >> 16) & 0xFF);
        dest[7]=(char)((data->packet_count >> 24) & 0xFF);
        dest[10]=dest[2];
        dest[11]=dest[3];
        memcpy (&(dest[12]), (gchar *)bdata, size-8);

        if(send(data->mmsh_socket, dest, (int)(size+4), 0) != size+4){
          g_printerr("buffer send fail.\n");
          free(dest);
          g_main_loop_quit(data->mmsh_loop);
          data->mmsh_status = MMSH_STATUS_NULL;
          gst_buffer_unref (buffer);
          gst_sample_unref (sample);
          return;
        }
        DEBUGLINE();
        data->packet_count++;
        free(dest);
        data->mmsh_status = MMSH_STATUS_ASF_DATA_SENDING;
    }
    gst_buffer_unref (buffer);
    gst_sample_unref (sample);
  }
}

static gboolean
  cb_print_position (GstElement *pipeline)
{
  gint64 pos;
  GstFormat format = GST_FORMAT_TIME;
  if (gst_element_query_position (pipeline, format, &pos)) {
    g_print ("Time: %" GST_TIME_FORMAT "\r",
      GST_TIME_ARGS (pos));
  }
  DEBUGLINE();
  /* call me again */
  return TRUE;
}

void notify(gpointer data)
{
  MmsData *mmsdata = (MmsData *)data;
  g_main_loop_quit(mmsdata->loop);
}

gpointer thread(gpointer data)
{
  GMainContext *c;
  GSource *s;
  MmsData *mmsdata = (MmsData *)data;

  DEBUGLINE();

  c = g_main_context_new();
  mmsdata->loop = g_main_loop_new(c, FALSE);
  //s = g_timeout_source_new(10000 * (mmsdata->number * 2 + 1));
  s = g_timeout_source_new_seconds  (polling_second);
  g_source_set_callback(s, (GSourceFunc)mms_loop, mmsdata, notify);
  g_source_attach(s, c);
  g_source_unref(s);

  g_main_loop_run(mmsdata->loop);
  g_message("done");
  DEBUGLINE();

  return NULL;
}

/* mmsh server system */
gpointer httpserver(gpointer in)
{
  CustomData *data = (CustomData *)in;

   WSADATA wsaData;
   SOCKET sock0;
   struct sockaddr_in addr;
   struct sockaddr_in client;
   int len;
   //SOCKET sock;
   BOOL yes = 1;
   
   char buf[2048];
   char inbuf[2048];

   WSAStartup(MAKEWORD(2,0), &wsaData);

   sock0 = socket(AF_INET, SOCK_STREAM, 0);
   if (sock0 == INVALID_SOCKET) {
	   DEBUG("socket : %d\n", WSAGetLastError());
	   return NULL;
   }

   addr.sin_family = AF_INET;
   addr.sin_port = htons(mms_ex_port);
   addr.sin_addr.S_un.S_addr = INADDR_ANY;

   setsockopt(sock0,
     SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof(yes));

   if (bind(sock0, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
	   DEBUG("bind : %d\n", WSAGetLastError());
	   return NULL;
   }

   if (listen(sock0, 5) != 0) {
	   DEBUG("listen : %d\n", WSAGetLastError());
	   return NULL;
   }
   DEBUGLINE();
   // 応答用HTTPメッセージ作成
   memset(buf, 0, sizeof(buf));
 
   while (1) {
     len = sizeof(client);
     data->mmsh_socket = accept(sock0, (struct sockaddr *)&client, &len);
     if (data->mmsh_socket == INVALID_SOCKET) {
	     DEBUG("accept : %d\n", WSAGetLastError());
	     break;
     }
     DEBUGLINE();
     data->mmsh_status = MMSH_STATUS_CONNECTED;

     memset(inbuf, 0, sizeof(inbuf));
     recv(data->mmsh_socket, inbuf, sizeof(inbuf), 0);
     g_print("%s", inbuf);

     // send HTTP response
     _snprintf(buf, sizeof(buf), 
       g_strdup_printf(
       "HTTP/1.0 200 OK\r\n"
       "Server: Rex/12.0.7601.17514\r\n"
       "Cache-Control: no-cache\r\n"
       "Pragma: no-cache\r\n"
       "Pragma: client-id=2236067900\r\n"
       "Pragma: features=\"broadcast,playlist\"\n"
       //"Content-Type: application/x-mms-framed\r\n"
       "Content-Type: application/vnd.ms.wms-hdr.asfv1\r\n"
       "Content-Length: %d\r\n"
       "Connection: Keep-Alive\r\n"
       "\r\n", data->asf_head_size));

     DEBUG("%d\r\n",(int)strlen(buf));
     if((int)strlen(buf) != send(data->mmsh_socket, buf, (int)strlen(buf), 0)){
       DEBUG("send error.\r\n");
     }

     data->mmsh_status = MMSH_STATUS_HTTP_HEADER_SEND;

     if(data->asf_status == ASF_STATUS_SET_HEADER){
       // send ASF header
       if((int)data->asf_head_size != send(data->mmsh_socket, data->asf_head_buffer, (int)data->asf_head_size, 0)){
         DEBUG("send error.\r\n");
       }
       data->mmsh_status = MMSH_STATUS_ASF_HEADER_SEND;

       // loop
       data->mmsh_loop = g_main_loop_new (NULL, FALSE);
       g_main_loop_run(data->mmsh_loop); 

     } else {
       DEBUG("asf header is not reached yet.\r\n");
     }

     closesocket(data->mmsh_socket);
     data->mmsh_status = MMSH_STATUS_NULL;
   }

   WSACleanup();

  return NULL;
}

int
  main (int   argc,
  char *argv[])
{
  CustomData data;
  GstBus *bus;
  GError *error = NULL;

  /* pad for "on request" pad */
  GstPadTemplate *pad_template;
  GstPad *mixer_pad;
  GstPad *src_pad;

  gint i; // mms source index

  gboolean res=TRUE;

  /* Option arguments */
  gboolean silent   = FALSE; //Silent mode
  gboolean exprt    = TRUE;  //default
  gboolean playback = FALSE; //playback mode
  gchar *vsize = "320x240"; // each video size "wwwxhhh"
  gchar *csize = "960x720"; // canvas size "wwwxhhh"

  gboolean debug_mode = TRUE; //debug mode
  
  gint bitrate = 500000; //bit/sec
  gint framerate = 15; //fps 
  gchar *bg = NULL;

  gint cwidth;
  gint cheight;
  gint vwidth;
  gint vheight;

  gboolean version  = FALSE;
  GOptionContext *ctx;
  GError *err = NULL;
  GOptionEntry entries[] = {
    { "silent", 'S', 0, G_OPTION_ARG_NONE, &silent,
    "no status information output", NULL },
    { "playback", 'P', 0, G_OPTION_ARG_NONE, &playback,
    "playback tiled stream", NULL },
    { "version", 'V', 0, G_OPTION_ARG_NONE, &version,
    "show version", NULL },
    { "debug", 'D', 0, G_OPTION_ARG_NONE, &debug_mode,
    "debug mode", NULL },
    { "latency", 'l', 0, G_OPTION_ARG_INT64, &latency,
    "set latency in nanosecound (default: 20000000000)", NULL },
    { "video-size", 0, 0, G_OPTION_ARG_STRING, &vsize,
    "set each video size (WWWxHHH)", NULL },
    { "canvas-size", 0, 0, G_OPTION_ARG_STRING, &csize,
    "set canvas size (WWWxHHH)", NULL },
    { "canvas-width", 0, 0, G_OPTION_ARG_INT, &canvas_width,
    "set number of horizontal tiles", NULL },
    { "num", 'n', 0, G_OPTION_ARG_INT, &stream_amount,
    "set how much stream we will accept", NULL },
    { "polling", 0, 0, G_OPTION_ARG_INT, &polling_second,
    "set polling interval", NULL },
    { "base-port", 'p', 0, G_OPTION_ARG_INT, &mms_base_port,
    "set first number of import port", NULL },
    { "ex-port", 'e', 0, G_OPTION_ARG_INT, &mms_ex_port,
    "set the number of export port", NULL },
    { "bitrate", 'b', 0, G_OPTION_ARG_INT, &bitrate,
    "set bit rate (bit per second)", NULL },
    { "framerate", 'f', 0, G_OPTION_ARG_INT, &framerate,
    "set integer frame rate (fps)", NULL },
    { "image", 'i', 0, G_OPTION_ARG_STRING, &bg,
    "set canvas image filepath (jpg only)", NULL },
    { NULL }
  };

  /* SIGINT trap */
  if(debug_mode){
    if (SIG_ERR == signal(SIGINT, sigcatch)) {
      g_print("failed to set signal handler.\n");
    }
  }

  /* we must initialise the threading system before using any
  * other GLib funtion, such as g_option_context_new() */
  if (!g_thread_supported ())
    g_thread_init (NULL);

  ctx = g_option_context_new ( "- Live Multi-Stream Tiling Encoder 多元舎");
  g_option_context_add_main_entries (ctx, entries, NULL);
  g_option_context_add_group (ctx, gst_init_get_option_group ());
  if (!g_option_context_parse (ctx, &argc, &argv, &err)) {
    g_print ("Failed to initialize: %s\n", err->message);
    g_error_free (err);
    return 1;
  }

  //exprt    = TRUE; // debug

  if(version){
    g_print("多元舎 %s", TAGENYA_VERSION);
    return 1;
  }

  if(silent){
    /* wrap print function to null */
    g_set_print_handler (noprint);
    g_set_printerr_handler (noprint);
  } 

  if(playback){
    g_print("playback mode\r\n");
    exprt = FALSE;
  }

  //size
  {
    char *i;
    char *j;
    char *s = (char *)malloc(sizeof vsize);
    strcpy(s, vsize);
    for(i=s; *i != 0x00; i++){
      if(*i=='x'){
        *i=0x00;
        j=i+1;
      }
    }
    vwidth = atoi(s);
    vheight = atoi(j);

  }
  {
    char *i;
    char *j;
    char *s = (char *)malloc(sizeof csize);
    strcpy(s, csize);
    for(i=s; *i != 0x00; i++){
      if(*i=='x'){
        *i=0x00;
        j=i+1;
      }
    }
    cwidth = atoi(s);
    cheight = atoi(j);

  }

  if(bg == NULL){
    char pathname[PATHNAME_SIZE];
    memset(pathname, '\0', PATHNAME_SIZE);
    _getcwd(pathname, PATHNAME_SIZE);
    bg = g_strdup_printf ("%s\\bg.jpg", pathname);
  }

  if(debug_mode){
    g_print("latency: %I64u\r\n", latency);
    //g_print("video-size: %s\r\n", vsize);
    //g_print("canvas-size: %s\r\n", csize);
    g_print("canvas_width: %d\r\n", canvas_width);
    g_print("stream_amount: %d\r\n", stream_amount);
    g_print("polling_second: %d\r\n", polling_second);
    g_print("mms_base_port: %d\r\n", mms_base_port);
    g_print("mms_ex_port: %d\r\n", mms_ex_port);
    g_print("bitrate: %d\r\n", bitrate);
    g_print("framerate: %d\r\n", framerate);
    g_print("canvas-image: %s\r\n", bg);

    g_print("vwidth: %d\r\n", vwidth);
    g_print("vheight: %d\r\n", vheight);
    g_print("cwidth: %d\r\n", cwidth);
    g_print("cheight: %d\r\n", cheight);
  }

  /* argment check */

  //if(exprt){
  //  /* print handler switch to error handler */
  //  g_set_print_handler (print_to_printerr);

  //}

  /* Initialisation */
  gst_init (&argc, &argv);

  data.loop = g_main_loop_new (NULL, FALSE);

  /* Create gstreamer elements */
  data.pipeline  = gst_pipeline_new ("player");

  // Background Image Stream
  data.source1   = gst_element_factory_make ("filesrc",      "source1");
  g_object_set(data.source1,"location", bg, NULL);
  data.queue1     = gst_element_factory_make ("queue",       "queue1"   );
  data.decoder1   = gst_element_factory_make ("decodebin",  "decoder1" );
  data.frz1       = gst_element_factory_make ("imagefreeze", "frz1"     );
  data.scale1     = gst_element_factory_make ("videoscale",  "scale1"   );
  data.rate1      = gst_element_factory_make ("videorate",   "rate1"    );
  data.filter1    = gst_element_factory_make ("capsfilter",  "filter1"  );
  data.videobox1  = gst_element_factory_make ("videobox",    "videobox1");

  /* audio stream */
  //dummy stream
  data.a_source   = gst_element_factory_make ("audiotestsrc",       "a_source"   );
  g_object_set(data.a_source, "wave", 4, NULL); // Silent
  data.a_queue1   = gst_element_factory_make ("queue",              "a_queue1"   );
  data.a_convert1 = gst_element_factory_make ("audioconvert",       "a_convert1" );
  data.a_resample1= gst_element_factory_make ("audioresample",      "a_resample1");
  data.a_filter1  = gst_element_factory_make ("capsfilter",         "a_filter1"  );

  /* video mixer */
  data.mixer      = gst_element_factory_make ("videomixer",        "mixer"   );
  //data.clrspace   = gst_element_factory_make ("ffmpegcolorspace",  "clrspace");
  data.clrspace   = gst_element_factory_make ("videoconvert",  "clrspace");
  data.sink       = gst_element_factory_make ("autovideosink",     "sink"    );
  g_object_set(data.sink,"message-forward", TRUE, NULL);

  /* audio mixer */
  data.a_mixer    = gst_element_factory_make ("adder",         "a_mixer"   );
  data.a_sink     = gst_element_factory_make ("autoaudiosink", "a_sink"    );
  g_object_set(data.a_sink,"message-forward", TRUE, NULL);

  /* to application */
  //data.encoder    = gst_element_factory_make ("ffenc_wmv2",  "encoder"   );
  data.encoder    = gst_element_factory_make ("avenc_wmv2",  "encoder"   );
  g_object_set(data.encoder,"bitrate", bitrate, NULL);
  //data.a_encoder  = gst_element_factory_make ("ffenc_wmav2", "a_encoder" );
  data.a_encoder  = gst_element_factory_make ("avenc_wmav2", "a_encoder" );
  data.asfmux     = gst_element_factory_make ("asfmux",      "asfmux"    );
  g_object_set(data.asfmux,"streamable", TRUE, NULL);
  data.appsink    = gst_element_factory_make ("appsink",     "appsink"   );

  if (!data.pipeline || !data.source1 || !data.sink || ! data.encoder || ! data.a_encoder || ! data.asfmux) {
    g_printerr ("One element could not be created. Exiting.\n");
    return -1;
  }
  DEBUGLINE();
  data.filtercaps1 = gst_caps_new_simple ("video/x-raw",
    // "format", GST_TYPE_FOURCC, GST_MAKE_FOURCC ('I', '4', '2', '0'),
    "format", G_TYPE_STRING, "I420",
    "framerate", GST_TYPE_FRACTION, framerate, 1,
    "pixel-aspect-ratio", GST_TYPE_FRACTION, 1, 1,
    "width", G_TYPE_INT, cwidth,
    "height", G_TYPE_INT, cheight,
    NULL);

  g_object_set (G_OBJECT (data.filter1), "caps", data.filtercaps1, NULL);
  gst_caps_unref (data.filtercaps1);

  data.a_filtercaps1 = gst_caps_new_simple ("audio/x-raw",
    "channels", G_TYPE_INT, 2,
    "rate", G_TYPE_INT, 44100,
    "endianness", G_TYPE_INT, 1234,
    "width", G_TYPE_INT, 32,
    //"depth", G_TYPE_INT, 16,
    NULL);

  g_object_set (G_OBJECT (data.a_filter1), "caps", data.a_filtercaps1, NULL);
  gst_caps_unref (data.a_filtercaps1);

  g_object_set(data.videobox1,"border-alpha",0,"top",0,"left",0,NULL);

  /* Set up the pipeline */

  /* Connect to the pad-added signal */
  g_signal_connect (data.decoder1, "pad-added", G_CALLBACK (pad_added_handler1), &data);

  /* we add a message handler */
  bus = gst_pipeline_get_bus (GST_PIPELINE (data.pipeline));
  gst_bus_add_watch (bus, bus_call, &data);
  gst_object_unref (bus);

  /* Configure appsink */
  g_object_set (data.appsink, "emit-signals", TRUE, NULL);
  g_signal_connect (data.appsink, "new-sample", G_CALLBACK (new_buffer), &data);

  /* we add all elements into the pipeline */
  gst_bin_add_many (GST_BIN (data.pipeline),
    data.source1,  
    data.filter1,  
    data.videobox1,
    data.queue1,  
    data.decoder1, 
    data.scale1,   
    data.rate1,   
    data.frz1,
    data.mixer, data.clrspace,
    data.a_source,
    data.a_filter1,
    data.a_queue1, 
    data.a_convert1, 
    data.a_resample1, 
    data.a_mixer, 
    NULL);

  if(exprt){
    gst_bin_add_many (GST_BIN (data.pipeline), data.encoder, data.a_encoder, data.asfmux, data.appsink, NULL);
  }else{
    gst_bin_add_many (GST_BIN (data.pipeline), data.sink, data.a_sink, NULL);
  }

  /* we link the elements together */
  res = gst_element_link_many (data.source1, data.queue1, data.decoder1, NULL);

  res = gst_element_link_many (data.scale1, data.frz1, data.rate1, data.filter1, data.videobox1, NULL);

  if(exprt){
    res = gst_element_link_many (data.mixer,  data.clrspace, data.encoder, NULL);
  }else{
    res = gst_element_link_many (data.mixer,  data.clrspace, data.sink, NULL);
  }

  res = gst_element_link_many (data.a_source, data.a_filter1, data.a_queue1, data.a_convert1, data.a_resample1, NULL);

  if(exprt){
    res = gst_element_link_many (data.a_mixer, data.a_encoder, NULL);
    res = gst_element_link_many (data.asfmux, data.appsink, NULL);
  }
  else{
    res = gst_element_link_many (data.a_mixer, data.a_sink, NULL);
  }

  /* link mixer manually */
  /* Manually link the Element, which has "Request" pads */
  pad_template   = gst_element_class_get_pad_template (GST_ELEMENT_GET_CLASS (data.mixer), "sink_%u");
  mixer_pad      = gst_element_request_pad (data.mixer, pad_template, NULL, NULL);
  g_print ("Obtained request pad %s for video mixer.\n", gst_pad_get_name (mixer_pad));
  src_pad  = gst_element_get_static_pad (data.videobox1, "src");
  if (gst_pad_link   (src_pad, mixer_pad) != GST_PAD_LINK_OK) {
    g_printerr       ("Cannot be linked.\n");
    gst_object_unref (data.pipeline);
    return -1;
  }
  gst_object_unref (src_pad);
  gst_object_unref (mixer_pad);
  gst_object_unref (pad_template);

  /* audio stream */
  pad_template   = gst_element_class_get_pad_template (GST_ELEMENT_GET_CLASS (data.a_mixer), "sink_%u");
  mixer_pad      = gst_element_request_pad (data.a_mixer, pad_template, NULL, NULL);
  g_print ("Obtained request pad %s for audio mixer.\n", gst_pad_get_name (mixer_pad));
  src_pad  = gst_element_get_static_pad (data.a_resample1, "src");
  if (gst_pad_link   (src_pad, mixer_pad) != GST_PAD_LINK_OK) {
    g_printerr       ("Cannot be linked.%d\n");
    gst_object_unref (data.pipeline);
    return -1;
  }
  gst_object_unref (src_pad);
  gst_object_unref (mixer_pad);
  gst_object_unref (pad_template);

  /* link asfmux manually */
  if(exprt){
    /* audio stream */
    pad_template   = gst_element_class_get_pad_template (GST_ELEMENT_GET_CLASS (data.asfmux), "audio_%u");
    mixer_pad      = gst_element_request_pad (data.asfmux, pad_template, NULL, NULL);
    DEBUG ("Obtained request pad %s for asf muxer.\n", gst_pad_get_name (mixer_pad));
    src_pad  = gst_element_get_static_pad (data.a_encoder, "src");
    if (gst_pad_link   (src_pad, mixer_pad) != GST_PAD_LINK_OK) {
      DEBUG       ("Cannot be linked.\n");
      gst_object_unref (data.pipeline);
      return -1;
    }
    gst_object_unref (src_pad);
    gst_object_unref (mixer_pad);
    gst_object_unref (pad_template);

    /* video stream */
    pad_template   = gst_element_class_get_pad_template (GST_ELEMENT_GET_CLASS (data.asfmux), "video_%u");
    mixer_pad      = gst_element_request_pad (data.asfmux, pad_template, NULL, NULL);
    DEBUG ("Obtained request pad %s for asf muxer.\n", gst_pad_get_name (mixer_pad));
    src_pad  = gst_element_get_static_pad (data.encoder, "src");
    if (gst_pad_link   (src_pad, mixer_pad) != GST_PAD_LINK_OK) {
      DEBUG       ("Cannot be linked.\n");
      gst_object_unref (data.pipeline);
      return -1;
    }
    gst_object_unref (src_pad);
    gst_object_unref (mixer_pad);
    gst_object_unref (pad_template);


  }

  DEBUGLINE();

  /** Mms Stream Negotiation */
  data.mms = (MmsData **)malloc(sizeof(MmsData *) * stream_amount); // ポインタ配列割り当て
  for ( i=0 ; i < stream_amount ; i++ ){
    /* Default mms stream elements criation. */
    /* allocate memory. */
    MmsData *mmsdata = (MmsData *)malloc(sizeof(MmsData)); // 実体
    memset (mmsdata, 0x00, sizeof(MmsData)); // いらないかも
    data.mms[i] = mmsdata;

    g_print ("Create mms element of #%d.\n",i);

    mmsdata->parent = &data;
    mmsdata->number = i;
    mmsdata->buffer_time = 0;

    /* Create the mms stream elements */
    mmsdata->pipeline = gst_pipeline_new (g_strdup_printf ("pipeline__%d"  , i));
    gst_pipeline_use_clock(GST_PIPELINE(mmsdata->pipeline), gst_pipeline_get_clock(GST_PIPELINE(data.pipeline)));

    /* source elements */
    mmsdata->source     = gst_element_factory_make ("mmssrc",       g_strdup_printf ("source__%d"  , i)); //あんすこ２
    //g_object_set(mmsdata->source,"location","mms://win.global.playstream.com/showcase/mathtv/trig_4.5_350k.wmv",NULL);
    //g_object_set(mmsdata->source,"location","mms://localhost:16556",NULL);
    mmsdata->mms_location = g_strdup_printf (MMS_HOST ":%d"  , mms_base_port + i);
    g_object_set(mmsdata->source,"location", g_strdup_printf("mms://%s", mmsdata->mms_location), NULL);

    mmsdata->queue      = gst_element_factory_make ("queue",        g_strdup_printf ("queue__%d"   , i)); 
    mmsdata->decoder    = gst_element_factory_make ("decodebin",   g_strdup_printf ("decoder__%d" , i));

    mmsdata->v_queue    = gst_element_factory_make ("queue",        g_strdup_printf ("v_queue__%d"   , i));
    mmsdata->scale      = gst_element_factory_make ("videoscale",   g_strdup_printf ("scale__%d"   , i));
    mmsdata->colorspace = gst_element_factory_make ("videoconvert",   g_strdup_printf ("colorspace__%d"   , i));
    mmsdata->rate       = gst_element_factory_make ("videorate",    g_strdup_printf ("rate__%d"    , i));
    mmsdata->filter     = gst_element_factory_make ("capsfilter",   g_strdup_printf ("filter__%d"  , i));
    mmsdata->videobox   = gst_element_factory_make ("videobox",     g_strdup_printf ("videobox__%d", i));

    /* audio source */
    mmsdata->a_queue    = gst_element_factory_make ("queue",        g_strdup_printf ("a_queue__%d"   , i));
    mmsdata->a_convert  = gst_element_factory_make ("audioconvert", g_strdup_printf ("a_convert__%d" , i));
    mmsdata->a_resample = gst_element_factory_make ("audioresample",g_strdup_printf ("a_resample__%d", i));

    /* app */
    mmsdata->v_appsink  = gst_element_factory_make ("appsink",      g_strdup_printf ("v_appsink__%d" , i)); 
    mmsdata->v_appsrc   = gst_element_factory_make ("appsrc",       g_strdup_printf ("v_appsrc__%d"  , i));
    g_object_set (G_OBJECT (mmsdata->v_appsrc), "stream-type", 0, "format", GST_FORMAT_TIME, NULL);
    mmsdata->v_app_q    = gst_element_factory_make ("queue",        g_strdup_printf ("v_app_q__%d"   , i));
    mmsdata->a_appsink  = gst_element_factory_make ("appsink",      g_strdup_printf ("a_appsink__%d" , i));
    mmsdata->a_appsrc   = gst_element_factory_make ("appsrc",       g_strdup_printf ("a_appsrc__%d"  , i));
    g_object_set (G_OBJECT (mmsdata->a_appsrc), "stream-type", 0, "format", GST_FORMAT_TIME, NULL);
    mmsdata->a_app_q    = gst_element_factory_make ("queue",        g_strdup_printf ("a_app_q__%d"   , i));

    /* increase ref */
    gst_object_ref (mmsdata->source       );
    gst_object_ref (mmsdata->queue        );
    gst_object_ref (mmsdata->decoder      );
    gst_object_ref (mmsdata->v_queue      );
    gst_object_ref (mmsdata->scale        );
    gst_object_ref (mmsdata->rate         );
    gst_object_ref (mmsdata->filter       );
    gst_object_ref (mmsdata->videobox     );
    gst_object_ref (mmsdata->v_appsink    );
    gst_object_ref (mmsdata->a_queue      );
    gst_object_ref (mmsdata->a_convert    );
    gst_object_ref (mmsdata->a_resample   );
    gst_object_ref (mmsdata->a_appsink    );

    gst_object_ref (mmsdata->v_appsrc     );
    gst_object_ref (mmsdata->v_app_q      );
    gst_object_ref (mmsdata->a_appsrc     );
    gst_object_ref (mmsdata->a_app_q      );

    /* Create caps */
    mmsdata->filtercaps = gst_caps_new_simple (
      "video/x-raw",
      //"format", GST_TYPE_FOURCC, GST_MAKE_FOURCC ('I', '4', '2', '0'),
      "format", G_TYPE_STRING, "I420",
      "framerate", GST_TYPE_FRACTION, framerate, 1,
      "pixel-aspect-ratio", GST_TYPE_FRACTION, 1, 1,
      "width", G_TYPE_INT, vwidth,
      "height", G_TYPE_INT, vheight,
      NULL);

    g_object_set (G_OBJECT (mmsdata->filter), "caps", mmsdata->filtercaps, NULL);

    gst_caps_unref (mmsdata->filtercaps);

    mmsdata->filtercaps = gst_caps_new_simple ("audio/x-raw",
      "channels", G_TYPE_INT, 2,
      "rate", G_TYPE_INT, 44100,
      "endianness", G_TYPE_INT, 1234,
      "width", G_TYPE_INT, 32,
      //"depth", G_TYPE_INT, 16,
      NULL);

    g_object_set(mmsdata->videobox,"border-alpha",0,"top",0,"left",0,NULL);

    /* Connect to the pad-added signal */
    g_signal_connect (mmsdata->decoder, "pad-added", G_CALLBACK (mms_pad_added_handler), mmsdata);

    /* video appsink hander */
    g_object_set (mmsdata->v_appsink, "emit-signals", TRUE, NULL);
    g_signal_connect (mmsdata->v_appsink, "new-sample", G_CALLBACK (v_new_buffer), mmsdata);

    /* audio appsink hander */
    g_object_set (mmsdata->a_appsink, "emit-signals", TRUE, NULL);
    g_signal_connect (mmsdata->a_appsink, "new-sample", G_CALLBACK (a_new_buffer), mmsdata);

    mmsdata->v_appflag = FALSE;
    mmsdata->a_appflag = FALSE;

    mmsdata->prob_hd = 0;
    mmsdata->prob_hd_v_eos = 0;
    mmsdata->prob_hd_a_eos = 0;

    mmsdata->width = vwidth;
    mmsdata->height = vheight;

    DEBUGLINE();
    /* threading */
    g_thread_new(g_strdup_printf ("thread%d", i), thread, mmsdata);
    DEBUGLINE();

  }

  DEBUGLINE();

  /* Set the pipeline to "playing" state*/
  gst_element_set_state (data.pipeline, GST_STATE_PLAYING);

  DEBUGLINE();

  /* set latency */
  if (!gst_element_send_event (data.pipeline, gst_event_new_latency (latency)))
    g_warning ("latency set failed\n");

  /* HTTP Server */
  memset(data.asf_head_buffer,0x00,ASF_HEADER_BUFSIZE);
  data.mmsh_status = MMSH_STATUS_NULL;
  data.mmsh_socket = (SOCKET)NULL;
  data.packet_count=0;
  g_thread_new("server", httpserver, &data);

  /* Iterate */
  DEBUG ("Running...\n");
  //g_timeout_add (200, (GSourceFunc) cb_print_position, data.pipeline);
  g_timeout_add (200, (GSourceFunc) print_element_info, &data);
  g_main_loop_run (data.loop);

  /* Out of the main loop, clean up nicely */
  g_print ("Returned, stopping playback\n");
  gst_element_set_state (data.pipeline, GST_STATE_NULL);

  g_print ("Deleting pipeline\n");
  gst_object_unref (GST_OBJECT (data.pipeline));


  return 0;
}


/* ©なん実企画部 ©Osashimitoolz 2013 */
