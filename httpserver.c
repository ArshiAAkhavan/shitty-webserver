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
  mthread_init_pool(parallelism_level,&log_queue, request_handler);
#else
  mprocess_init_pool(parallelism_level, request_handler);
#endif
}
/*
 * Global configuration variables.
 * You need to use these in your implementation of handle_files_request and
 * handle_proxy_request. Their values are set up in main() using the
 * command line arguments (already implemented for you).
 */
int parallelism_level = 5;
int server_port;
char *server_files_directory;
char *server_proxy_hostname;
int server_proxy_port;

/*
 * Serves the contents the file stored at `path` to the client socket `fd`.
 * It is the caller's responsibility to ensure that the file stored at `path`
 * exists. You can change these functions to anything you want.
 *
 * ATTENTION: Be careful to optimize your code. Judge is
 *            sensitive to time-out errors.
 */
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
  http_send_header(fd, "Content-Length", content_len); // Change this too
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
 * list of files in the directory with links to each. 4) Send a 404 Not Found
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

  /* Remove beginning `./` */
  char *path =
      malloc(2 + strlen(server_files_directory) + strlen(request->path) + 1);
  path[0] = '.';
  path[1] = '/';
  memcpy(path + 2, server_files_directory, strlen(server_files_directory) + 1);
  memcpy(path + 2 + strlen(server_files_directory), request->path,
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

/*
 * Opens a connection to the proxy target (hostname=server_proxy_hostname and
 * port=server_proxy_port) and relays traffic to/from the stream client_fd and
 * the proxy target. HTTP requests from the client (client_fd) should be sent
 * to the proxy target, and HTTP responses from the proxy target should be
 * sent to the client (client_fd).
 *
 *   +--------+     +------------+     +--------------+
 *   | client | <-> | httpserver | <-> | proxy target |
 *   +--------+     +------------+     +--------------+
 */
void handle_proxy_request(int client_fd) {

  /*
   * The code below does a DNS lookup of server_proxy_hostname and
   * opens a connection to it. Please do not modify.
   */

  struct sockaddr_in target_address;
  memset(&target_address, 0, sizeof(target_address));
  target_address.sin_family = AF_INET;
  target_address.sin_port = htons(server_proxy_port);

  struct hostent *target_dns_entry =
      gethostbyname2(server_proxy_hostname, AF_INET);

  int target_fd = socket(PF_INET, SOCK_STREAM, 0);
  if (target_fd == -1) {
    fprintf(stderr, "Failed to create a new socket: error %d: %s\n", errno,
            strerror(errno));
    close(client_fd);
    exit(errno);
  }

  if (target_dns_entry == NULL) {
    fprintf(stderr, "Cannot find host: %s\n", server_proxy_hostname);
    close(target_fd);
    close(client_fd);
    exit(ENXIO);
  }

  char *dns_address = target_dns_entry->h_addr_list[0];

  memcpy(&target_address.sin_addr, dns_address,
         sizeof(target_address.sin_addr));
  int connection_status = connect(target_fd, (struct sockaddr *)&target_address,
                                  sizeof(target_address));

  if (connection_status < 0) {
    /* Dummy request parsing, just to be compliant. */
    http_request_parse(client_fd);

    http_start_response(client_fd, 502);
    http_send_header(client_fd, "Content-Type", "text/html");
    http_end_headers(client_fd);
    http_send_string(client_fd,
                     "<center><h1>502 Bad Gateway</h1><hr></center>");
    close(target_fd);
    close(client_fd);
    return;
  }

  pthread_t threads[2];
  int pipe1[] = {client_fd, target_fd};
  pthread_create(threads, NULL, (void *)pipe_fd, pipe1);

  int pipe2[] = {target_fd, client_fd};
  pthread_create(threads + 1, NULL, (void *)pipe_fd, pipe2);

  pthread_join(threads[0], NULL);
  pthread_join(threads[1], NULL);

  close(target_fd);
  close(client_fd);
}

/*
 * Opens a TCP stream socket on all interfaces with port number PORTNO. Saves
 * the fd number of the server socket in *socket_number. For each accepted
 * connection, calls request_handler with the accepted fd number.
 */
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

  // mthread_init_pool(num_threads, request_handler);
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

    // mthread_submit_task(client_socket_number);
    submit_task(client_socket_number);
    // wq_push(&work_queue, client_socket_number);
    // pid_t pid= fork();
    // if (pid==0){
    //   printf("im the child");
    //   handle_files_request(client_socket_number);
    //   close(client_socket_number);
    //   exit(0);
    // }
    // close(client_socket_number);

    printf("closed connection from %s on port %d\n",
           inet_ntoa(client_address.sin_addr), client_address.sin_port);
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

  while (log_queue.size) {
    char *text=lq_pop(&log_queue);
    printf("%s\n",text);
  }

  exit(0);
}

char *USAGE =
    "Usage: ./httpserver --files www_directory/ --port 8000 [--num-threads "
    "5]\n"
    "       ./httpserver --proxy inst.eecs.berkeley.edu:80 --port 8000 "
    "[--num-threads 5]\n";

void exit_with_usage() {
  fprintf(stderr, "%s", USAGE);
  exit(EXIT_SUCCESS);
}

int main(int argc, char **argv) {
  signal(SIGINT, signal_callback_handler);
  signal(SIGPIPE, SIG_IGN);

  /* Default settings */
  server_port = 8000;
  void (*request_handler)(int) = NULL;

  int i;
  for (i = 1; i < argc; i++) {
    if (strcmp("--files", argv[i]) == 0) {
      request_handler = handle_files_request;
      free(server_files_directory);
      server_files_directory = argv[++i];
      if (!server_files_directory) {
        fprintf(stderr, "Expected argument after --files\n");
        exit_with_usage();
      }
    } else if (strcmp("--proxy", argv[i]) == 0) {
      request_handler = handle_proxy_request;

      char *proxy_target = argv[++i];
      if (!proxy_target) {
        fprintf(stderr, "Expected argument after --proxy\n");
        exit_with_usage();
      }

      char *colon_pointer = strchr(proxy_target, ':');
      if (colon_pointer != NULL) {
        *colon_pointer = '\0';
        server_proxy_hostname = proxy_target;
        server_proxy_port = atoi(colon_pointer + 1);
      } else {
        server_proxy_hostname = proxy_target;
        server_proxy_port = 80;
      }
    } else if (strcmp("--port", argv[i]) == 0) {
      char *server_port_string = argv[++i];
      if (!server_port_string) {
        fprintf(stderr, "Expected argument after --port\n");
        exit_with_usage();
      }
      server_port = atoi(server_port_string);
    } else if (strcmp("--num-threads", argv[i]) == 0) {
      char *num_threads_str = argv[++i];
      if (!num_threads_str || (parallelism_level = atoi(num_threads_str)) < 1) {
        fprintf(stderr, "Expected positive integer after --num-threads\n");
        exit_with_usage();
      }
    } else if (strcmp("--help", argv[i]) == 0) {
      exit_with_usage();
    } else {
      fprintf(stderr, "Unrecognized option: %s\n", argv[i]);
      exit_with_usage();
    }
  }

  if (server_files_directory == NULL && server_proxy_hostname == NULL) {
    fprintf(stderr, "Please specify either \"--files [DIRECTORY]\" or \n"
                    "                      \"--proxy [HOSTNAME:PORT]\"\n");
    exit_with_usage();
  }

  serve_forever(&server_fd, request_handler);

  return EXIT_SUCCESS;
}
