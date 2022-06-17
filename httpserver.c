#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "libhttp.h"
#include "lq.h"
#include "mprocess.h"
#include "mthread.h"

int parallelism_level = 1;
int server_port;
char *server_files_directory;
char *log_path;
char *server_proxy_hostname;
int server_proxy_port;
lq_t log_queue;

void submit_task(int client_socket_number) {
#ifdef MTHREAD
  mthread_submit_task(client_socket_number);
#else
  mprocess_submit_task(client_socket_number);
#endif
}

void init_pool(int parallelism_level, void (*request_handler)(int)) {
#ifdef MTHREAD
  mthread_init_pool(parallelism_level, &log_queue, request_handler);
#else
  mprocess_init_pool(parallelism_level, &log_queue, request_handler);
#endif
}

void serve_file(int fd, char *path) {
  static int buff_size = 1024;
  char buff[buff_size];
  FILE *content = fopen(path, "r");

  // get size of the requested file
  char content_len[20];
  fseek(content, 0L, SEEK_END);
  sprintf(content_len, "%lu", ftell(content));
  fseek(content, 0L, SEEK_SET);

  http_start_response(fd, 200);
  http_send_header(fd, "Content-Type", http_get_mime_type(path));
  http_send_header(fd, "Content-Length", content_len);
  http_end_headers(fd);

  int num_elem;
  while ((num_elem = fread(buff, 1, buff_size, content)) == buff_size)
    http_send_data(fd, buff, buff_size);
  http_send_data(fd, buff, num_elem);

  fclose(content);
}

void serve_directory(int fd, char *path, char *base_path) {

  // check if index.html exists
  char path_with_index[1024] = {0};

  strcat(path_with_index, path);
  if (path[strlen(path) - 1] != '/')
    strcat(path_with_index, "/");
  strcat(path_with_index, "index.html");

  struct stat sb = {0};
  stat(path_with_index, &sb);
  if (S_ISREG(sb.st_mode))
    return serve_file(fd, path_with_index);

  // serve directory
  http_start_response(fd, 200);
  http_send_header(fd, "Content-Type", http_get_mime_type(".html"));
  http_end_headers(fd);

  DIR *d;
  d = opendir(path);
  if (!d)
    return;

  struct dirent *dir;
  while ((dir = readdir(d)) != NULL) {
    http_send_string(fd, "<a href=\"");
    http_send_string(fd, base_path);
    if (base_path[strlen(base_path) - 1] != '/')
      http_send_string(fd, "/");
    http_send_string(fd, dir->d_name);
    http_send_string(fd, "\"><h2>");
    http_send_string(fd, dir->d_name);
    http_send_string(fd, "</h2></a>");
  }
  closedir(d);
}

void serve_404(int fd, char *path) {
  http_start_response(fd, 404);
  http_send_header(fd, "Content-Type", "text/html");
  http_end_headers(fd);
  http_send_string(fd, "<center>"
                       "<h1>path not found</h1>"
                       "<hr>"
                       "</center>");
}

/*
 * Reads an HTTP request from stream (fd), and writes an HTTP response
 * containing:
 *
 *   1) If user requested an existing file, respond with the file
 *   2) If user requested a directory and index.html exists in the directory,
 *      send the index.html file.
 *   3) If user requested a directory and index.html doesn't exist, send a
 * list of files in the directory with links to each.
 *   4) Send a 404 Not Found
 * response.
 *
 *   Closes the client socket (fd) when finished.
 */
void handle_files_request(int fd) {

  struct http_request *request = http_request_parse(fd);
  if (request == NULL || request->path[0] != '/') {
    http_start_response(fd, 400);
    http_send_header(fd, "Content-Type", "text/html");
    http_end_headers(fd);
    close(fd);
    return;
  }

  if (strstr(request->path, "..") != NULL) {
    http_start_response(fd, 403);
    http_send_header(fd, "Content-Type", "text/html");
    http_end_headers(fd);
    close(fd);
    return;
  }

  char *path =
      malloc(strlen(server_files_directory) + strlen(request->path) + 1);
  memcpy(path, server_files_directory, strlen(server_files_directory) + 1);
  memcpy(path + strlen(server_files_directory), request->path,
         strlen(request->path) + 1);

  struct stat sb = {0};
  stat(path, &sb);

  if (S_ISREG(sb.st_mode))
    serve_file(fd, path);
  else if (S_ISDIR(sb.st_mode))
    serve_directory(fd, path, request->path);
  else
    serve_404(fd, path);
  close(fd);
}

void pipe_fd(const int *fds) {
  static int buff_size = 1024 * 1024;
  char buff[buff_size];

  int src = fds[0];
  int dst = fds[1];

  while (1) {
    ssize_t bytes = recv(src, buff, buff_size, 0);
    if (bytes <= 0) {
      shutdown(src, SHUT_RD);
      shutdown(dst, SHUT_WR);
      return;
    }
    http_send_data(dst, buff, bytes);
  }
}

_Noreturn void serve_forever(int *socket_number, void (*request_handler)(int)) {

  struct sockaddr_in server_address, client_address;
  size_t client_address_length = sizeof(client_address);
  int client_socket_number;

  *socket_number = socket(PF_INET, SOCK_STREAM, 0);
  if (*socket_number == -1) {
    perror("Failed to create a new socket");
    exit(errno);
  }

  int socket_option = 1;
  if (setsockopt(*socket_number, SOL_SOCKET, SO_REUSEADDR, &socket_option,
                 sizeof(socket_option)) == -1) {
    perror("Failed to set socket options");
    exit(errno);
  }

  memset(&server_address, 0, sizeof(server_address));
  server_address.sin_family = AF_INET;
  server_address.sin_addr.s_addr = INADDR_ANY;
  server_address.sin_port = htons(server_port);

  if (bind(*socket_number, (struct sockaddr *)&server_address,
           sizeof(server_address)) == -1) {
    perror("Failed to bind on socket");
    exit(errno);
  }

  if (listen(*socket_number, 1024) == -1) {
    perror("Failed to listen on socket");
    exit(errno);
  }

  printf("Listening on port %d...\n", server_port);

  init_pool(parallelism_level, request_handler);
  while (1) {
    client_socket_number =
        accept(*socket_number, (struct sockaddr *)&client_address,
               (socklen_t *)&client_address_length);
    if (client_socket_number < 0) {
      perror("Error accepting socket");
      continue;
    }

    printf("Accepted connection from %s on port %d\n",
           inet_ntoa(client_address.sin_addr), client_address.sin_port);

    submit_task(client_socket_number);
  }

  shutdown(*socket_number, SHUT_RDWR);
  close(*socket_number);
}

int server_fd;

void signal_callback_handler(int signum) {
  printf("Caught signal %d: %s\n", signum, strsignal(signum));
  printf("Closing socket %d\n", server_fd);
  if (close(server_fd) < 0)
    perror("Failed to close server_fd (ignoring)\n");

  FILE *log_file = fopen(log_path, "a+");
  while (log_queue.size) {
    char *text = lq_pop(&log_queue);
    printf("%s\n", text);
    fprintf(log_file, "%s\n", text);
  }

  exit(0);
}

const char SERVER_CONF_PATH[] = "/etc/httpserver.conf";
const int MAX_LINE_LENGTH = 100;

int main(int argc, char **argv) {
  signal(SIGINT, signal_callback_handler);
  signal(SIGPIPE, SIG_IGN);

  /* Default settings */
  server_port = 8000;
  void (*request_handler)(int) = handle_files_request;

  FILE *configs = fopen(SERVER_CONF_PATH, "r");
  if (!configs) {
    perror("unable to load config files");
  }
  char line[MAX_LINE_LENGTH];
  char value[MAX_LINE_LENGTH];
  char key[MAX_LINE_LENGTH];
  while (fgets(line, MAX_LINE_LENGTH, configs)) {
    sscanf(line, "%[^:]s", key);
    sscanf(line, "%*s%s", value);

    if (!strcmp(key, "port")) {
      server_port = atoi(value);
    } else if (!strcmp(key, "files")) {
      server_files_directory = malloc(sizeof(char) * strlen(value) + 1);
      memcpy(server_files_directory, value, sizeof(char) * strlen(value) + 1);
    } else if (!strcmp(key, "concurrency_level")) {
      parallelism_level = atoi(value);
    } else if (!strcmp(key, "log_path")) {
      log_path = malloc(sizeof(char) * strlen(value) + 1);
      memcpy(log_path, value, sizeof(char) * strlen(value) + 1);
    // } else if (!strcmp(key,"proxy_hostname")){
    //   server_proxy_hostname = malloc(sizeof(char) * strlen(value) + 1);
    //   memcpy(server_proxy_hostname, value, sizeof(char) * strlen(value) + 1);
    // } else if (!strcmp(key,"mode")){
    //   if (!strcmp(value,"proxy")){
    //     request_handler=handle_proxy_request;
    //   }
    }
  }

  serve_forever(&server_fd, request_handler);

  return EXIT_SUCCESS;
}
