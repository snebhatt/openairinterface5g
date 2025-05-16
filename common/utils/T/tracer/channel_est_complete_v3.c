#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include "database.h"
#include "event.h"
#include "handler.h"
#include "logger/logger.h"
#include "utils.h"
#include "event_selector.h"
#include "configuration.h"
#include <stdint.h>
#include <stdbool.h>

#define NUM_ELEMENTS 4
#define FILENAME_MAX_LENGTH 100

/* this function sends the activated traces to the nr-softmodem */
void activate_traces(int socket, int number_of_events, int *is_on)
{
  char t = 1;
  if (socket_send(socket, &t, 1) == -1 || socket_send(socket, &number_of_events, sizeof(int)) == -1
      || socket_send(socket, is_on, number_of_events * sizeof(int)) == -1)
    abort();
}

void usage(void)
{
  printf(
      "options:\n"
      "    -d <database file>        this option is mandatory\n"
      "    -ip <host>                connect to given IP address (default %s)\n"
      "    -p <port>                 connect to given port (default %d)\n",
      DEFAULT_REMOTE_IP,
      DEFAULT_REMOTE_PORT);
  exit(1);
}

/* Write the common JSON header for each signal */ 
void write_common_json_header(FILE *file, const char *name, int antenna, int ofdm, int ports, int symb)
{
  fprintf(file, "{\n");
  fprintf(file, "  \"name\": \"%s\",\n", name);
  fprintf(file, "  \"Antenna\": %d,\n", antenna);
  fprintf(file, "  \"Ofdm_symbol_size\": %d,\n", ofdm);
  fprintf(file, "  \"ports\": %d,\n", ports);
  fprintf(file, "  \"symb\": %d,\n", symb);
}

/* for printing each signal into I/Q values to json */
void print_signal_to_json(FILE *file, const char* sig_name, int sig_val, event e, int is_last, int antenna, int ofdm, int ports, int symb) {
  fflush(file);
  write_common_json_header(file, sig_name, antenna, ofdm, ports, symb);
  fprintf(file, "  \"signal\": [\n");  
  const int32_t *samples = (const int32_t *)e.e[sig_val].b;
  int bsize = e.e[sig_val].bsize;


  if (!samples || bsize % sizeof(int32_t) != 0) {
    fprintf(stderr, "Invalid buffer for signal: %s\n", sig_name);
    fprintf(file, "  ]\n  }%s", is_last ? "\n" : ",\n");
    return;
  }

  for (int ant = 0; ant < antenna; ant++) {
    const int32_t *ant_samples = &samples[ant * ofdm * symb];
    for (int i = 0; i < ofdm * symb; i++) {
        int16_t i_part = (int16_t)(ant_samples[i] & 0xFFFF);
        int16_t q_part = (int16_t)((ant_samples[i] >> 16) & 0xFFFF);
        fprintf(file, "    [%d, %d]%s\n", i_part, q_part, (i < ofdm * symb - 1) ? "," : "");
    }
    fprintf(file, (ant < antenna - 1) ? "," : "");
 }

  fprintf(file, "  ]\n");
  fprintf(file, "}\n%s", is_last ? "" : ",\n");
}


int main(int n, char **v)
{
  char *database_filename = NULL;
  void *database;
  char *ip = DEFAULT_REMOTE_IP;
  int port = DEFAULT_REMOTE_PORT;
  int *is_on;
  int number_of_events;
  int i;
  int socket;
  int channel_est_id;
  database_event_format f;
  int antenna;
  int ofdm;
  int symb;
  int ports;
  int srs_received_signal; 
  int srs_estimated_channel_freq;
  int srs_estimated_channel_time;
  int srs_estimated_channel_time_shifted;
  int srs_generated_signal;

  /* write on a socket fails if the other end is closed and we get SIGPIPE */
  if (signal(SIGPIPE, SIG_IGN) == SIG_ERR)
    abort();

  /* parse command line options */
  for (i = 1; i < n; i++) {
    if (!strcmp(v[i], "-h") || !strcmp(v[i], "--help"))
      usage();
    if (!strcmp(v[i], "-d")) {
      if (i > n - 2)
        usage();
      database_filename = v[++i];
      continue;
    }
    if (!strcmp(v[i], "-ip")) {
      if (i > n - 2)
        usage();
      ip = v[++i];
      continue;
    }
    if (!strcmp(v[i], "-p")) {
      if (i > n - 2)
        usage();
      port = atoi(v[++i]);
      continue;
    }
    usage();
  }

  if (database_filename == NULL) {
    printf("ERROR: provide a database file (-d)\n");
    exit(1);
  }

  /* load the database T_messages.txt */
  database = parse_database(database_filename);
  load_config_file(database_filename);

  /* an array of int for all the events defined in the database is needed */
  number_of_events = number_of_ids(database);
  is_on = calloc(number_of_events, sizeof(int));
  if (is_on == NULL)
    abort();

  /* activate the GNB_PHY_UL_TIME_CHANNEL_ESTIMATE_COMPLETE_V2 trace in this array */
  on_off(database, "GNB_PHY_UL_TIME_CHANNEL_ESTIMATE_COMPLETE_V2", is_on, 1);

  /* connect to the nr-softmodem */
  socket = connect_to(ip, port);

  /* activate the trace GNB_PHY_UL_TIME_CHANNEL_ESTIMATE_COMPLETE_V2 in the nr-softmodem */
  activate_traces(socket, number_of_events, is_on);

  free(is_on);

  /* get the format of the GNB_PHY_UL_TIME_CHANNEL_ESTIMATE_COMPLETE_V2 trace */
  channel_est_id = event_id_from_name(database, "GNB_PHY_UL_TIME_CHANNEL_ESTIMATE_COMPLETE_V2");
  f = get_format(database, channel_est_id);

/* this macro looks for a particular element and checks its type */
#define G(var_name, var_type, var)           \
  if (!strcmp(f.name[i], var_name)) {        \
    if (strcmp(f.type[i], var_type)) {       \
      printf("bad type for %s\n", var_name); \
      exit(1);                               \
    }                                        \
    var = i;                                 \
    continue;                                \
  }

  /* get the elements of the GNB_PHY_UL_TIME_CHANNEL_ESTIMATE_COMPLETE_V2 trace
   * the value is an index in the event, see below
   */
  for (i = 0; i < f.count; i++) {
    G("antenna", "int", antenna);
    G("ofdm", "int", ofdm);
    G("symb", "int", symb);
    G("ports", "int", ports);
    G("srs_received_signal", "buffer", srs_received_signal);
    G("srs_estimated_channel_freq", "buffer", srs_estimated_channel_freq);
    G("srs_estimated_channel_time", "buffer", srs_estimated_channel_time);
    G("srs_estimated_channel_time_shifted", "buffer", srs_estimated_channel_time_shifted);
    G("srs_generated_signal", "buffer", srs_generated_signal);
  }

  /* a buffer needed to receive events from the nr-softmodem */
  OBUF ebuf = {osize : 0, omaxsize : 0, obuf : NULL};

  /* time based logging */
  time_t last_log_time = 0;
  int log_interval_sec = 5;

  /* read events */
  while (1) {
    event e;
    e = get_event(socket, &ebuf, database);
    if (e.type == -1)
      break;
    if (e.type == channel_est_id) {

      antenna = e.e[antenna].i;
      ofdm = e.e[ofdm].i;
      symb = e.e[symb].i;
      ports = e.e[ports].i;
    
      /* time limit to print one event at a time*/ 
      time_t now = time(NULL);
      if (difftime(now, last_log_time) < log_interval_sec) continue;
      last_log_time = now;

      /*filename creation with timestamp*/
      time_t t;
      time(&t);
      struct tm *tm_info = localtime(&t);
      char filename[FILENAME_MAX_LENGTH];
      strftime(filename, sizeof(filename), "T_%Y%m%d_%H:%M:%S.json", tm_info);

      FILE *file = fopen(filename, "a");
      if (file == NULL) {
        perror("Error opening file");
      }

      /* Funtion calls for printing the buffer values */
      fprintf(file, "[\n");

      typedef struct {
        int value;
        const char* name;
      } Signal;

      Signal signals[] = {
        {srs_received_signal, "srs_received_signal"},
        {srs_estimated_channel_freq, "srs_estimated_channel_freq"},
        {srs_estimated_channel_time,"srs_estimated_channel_time"},
        {srs_estimated_channel_time_shifted,"srs_estimated_channel_time_shifted"},
        {srs_generated_signal,"srs_generated_signal"}
      };

      int num_signals = sizeof(signals) / sizeof(signals[0]);
      
      for (int i = 0; i < num_signals; i++) {
        int sig_val = signals[i].value;
        const char *sig_name = signals[i].name; 
        bool is_last = (i == num_signals - 1);
        print_signal_to_json(file, sig_name, sig_val, e, is_last, antenna, ofdm, ports, symb);     
      }

      fprintf(file, "]");
      fclose(file);       
    }   
  }
  return 0;
}