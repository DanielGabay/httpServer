#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <ctype.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include "threadpool.h"

/**define of sizes:*/
#define BUFF_SIZE 4000
#define MAX_ERROR_SIZE 320
#define MAX_BODY_SIZE 150
#define MAX_READ 1024
#define MAX_HEADER 350

/**define of erros*/
#define FOUND 302
#define BAD_REQUEST 400
#define FORBIDDEN 403
#define NOT_FOUND 404
#define NOT_SUPPORTED 501
#define USAGE_ERROR "Usage: server <port> <pool-size> <max-number-of-request>\n"

/**define of "private" methods internal uses*/
#define IS_A_NUMBER 0
#define NOT_A_NUMBER -1
#define VALID_PREMISSION 1
#define INVALID_PREMISSION -1
#define INTERNAL_ERROR 0
#define FAILED -1

/**define for headers*/
#define SERVER "webserver/1.0"
#define PROTOCOL "HTTP/1.0"
#define RFC1123FMT "%a, %d %b %Y %H:%M:%S GMT"
#define ERROR_RESPONSE_HTML "<HTML><HEAD><TITLE>%d %s</TITLE></HEAD>\r\n<BODY><H4>%d %s</H4>\r\n%s\r\n</BODY></HTML>\r\n"

/**Dir content defines*/
#define DIR_CONTENT_START "<HTML>\r\n<HEAD><TITLE>Index of %s</TITLE></HEAD>\r\n<BODY>\r\n<H4>Index of %s</H4>\r\n<table CELLSPACING=8>\r\n<tr><th>Name</th><th>Last Modified</th><th>Size</th></tr>\r\n"
#define DIR_CONTENT_FOLDER "<tr>\r\n<td><A HREF=\"%s/\">%s</td>\r\n<td>%s</td>\r\n</tr>\r\n"
#define DIR_CONTENT_FILE "<tr>\r\n<td><A HREF=\"%s""\">%s</td>\r\n<td>%s</td>\r\n<td>%zd</td>\r\n</tr>\r\n"
#define DIR_CONTENT_END "</table><HR>\r\n<ADDRESS>%s</ADDRESS>\r\n</BODY></HTML>\r\n"

#define INDEX_FILE "index.html"
/**
 * @author: Daniel Gabay
 * server.c
 * -----------------------------------------------
 * This program implements an HTTP server.
 * The server supports only GET method, request protocol can by sent by: HTTP/1.0 & HTTP/1.1,
 * but the response is always HTTP/1.0.
 * The server is able to:
 *      1) read & analyze client's request.
 *      2) Constructs an HTTP response based on client's request.
 *      3) Sends the response to the client.
 *
 * The server should handle the connections with the clients (using TCP) and creates a socket
 * for each client it talks to. In order to enable multithreaded program,
 * the server should create threads that handle the connections withthe clients.
 * Since, the server should maintain a limited number of threads, it constructs a thread pool.
 * Command line usage: server <port> <pool-size> <max-number-of-request>
 * The response of the server depends on the the client's request.
 * There are 3 main response categories:
 *      1)Error -> internal error or client's request error
 *      2)File content -> when requesting a file that the client has premission to read, the server will send it back.
 *      3)Dir content -> an HTML table contains all folder content
 */

/**forward declaration*/
int is_a_number(char *str);

int create_server(int port);

int handel_request(void *arg);

char *get_mime_type(char *name);

void send_file(char *path, struct stat *statbuf, int sockfd);

void construct_headers(char *res, int status, char *title, char *location, char *mime, int length, char *last_modified);

void send_dir_content(char *path, struct stat *statbuf, int sockfd);

void send_error_response(char *path, int status, int sockfd);

void send_internal_error500(int sockfd);

int folderExecutePremession(char *path);

int dirContentSizeToAllocate(char *path);

int main(int argc, char *argv[]) {

    /*user must insert 4 arguments*/
    if (argc != 4) {
        printf(USAGE_ERROR);
        exit(EXIT_FAILURE);
    }
    /*check that argv[1],argv[2],argv[3] is numbers*/
    for (int i = 1; i < argc; i++)
        if (is_a_number(argv[i]) == NOT_A_NUMBER) {
            printf(USAGE_ERROR);
            exit(EXIT_FAILURE);
        }

    int port = atoi(argv[1]);
    int poolSize = atoi(argv[2]);
    int maxNumOfRequests = atoi(argv[3]);

    if(port <= 0 || poolSize <= 0 || maxNumOfRequests <= 0){
        printf(USAGE_ERROR);
        exit(EXIT_FAILURE);
    }

    int *sock_fds = (int *) malloc(sizeof(int) * maxNumOfRequests);
    if (sock_fds == NULL) {
        printf("malloc sock_fds array failed\n");
        exit(EXIT_FAILURE);
    }

    threadpool *tp = create_threadpool(poolSize);
    if (tp == NULL) {
        printf(USAGE_ERROR);
        free(sock_fds);
        exit(EXIT_FAILURE);
    }

    signal(SIGPIPE, SIG_IGN); //prevent SIGPIPE raise

    int main_sockfd = create_server(port);
    if (main_sockfd == FAILED) {
        destroy_threadpool(tp);
        free(sock_fds);
        exit(EXIT_FAILURE);
    }
    for (int i = 0; i < maxNumOfRequests; i++) {
        if ((sock_fds[i] = accept(main_sockfd, NULL, NULL)) < 0) {
            perror("accept");
            break;
        }
        dispatch(tp, handel_request, &sock_fds[i]);
    }
    destroy_threadpool(tp);
    shutdown(main_sockfd, SHUT_RDWR);
    close(main_sockfd);
    free(sock_fds);
    return 0;
}

/**this method get port num and initialize the welcome socket. on succsess, the sockfd will return. o.w, exit program*/
int create_server(int port) {
    int welcome_sock_fd;
    struct sockaddr_in srv;

    //Creating socket fd
    if ((welcome_sock_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        return FAILED;
    }

    srv.sin_family = AF_INET;
    srv.sin_port = htons(port);
    srv.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(welcome_sock_fd, (struct sockaddr *) &srv, sizeof(srv)) < 0) {
        perror("bind failed\n");
        return FAILED;
    }
    if (listen(welcome_sock_fd, 5) < 0) {
        perror("listen failed");
        return FAILED;
    }
    return welcome_sock_fd;
}

/**this method is used by threads when server get request from some client
 *this method reads the request and sends response to the client
*/
int handel_request(void *fd) {
    if(fd == NULL)
        return 0;
    int new_sockfd = *(int *) fd;
    char *buff = (char *) malloc(sizeof(char) * BUFF_SIZE);
    if (!buff) {
        send_internal_error500(new_sockfd);
        return 0;
    }
    bzero(buff, BUFF_SIZE);
    struct stat stat_buffer;
    int path_len = 0, folder_execute = 0;

    //reading client request
    if ((read(new_sockfd, buff, BUFF_SIZE - 1)) < 0) {
        perror("read\n");
        send_internal_error500(new_sockfd);
        free(buff);
        return 0;
    }

    /**1st check: there a 3 tokens at the first row and the last one is a valid http protocol*/
    char *method = strtok(buff, " ");
    char *path = strtok(NULL, " ");
    char *protocol = strtok(NULL, "\r\n");
    if (method == NULL || path == NULL || protocol == NULL ||
        (strcmp(protocol, "HTTP/1.0") != 0 && strcmp(protocol, "HTTP/1.1") != 0)) {
        send_error_response(path, BAD_REQUEST, new_sockfd);
        free(buff);
        return 0;
    }
    /**2nd check: support only GET method*/
    if (strcmp(method, "GET") != 0) {
        send_error_response(path, NOT_SUPPORTED, new_sockfd);
        free(buff);
        return 0;
    }
    path_len = strlen(path);
    if (path_len > 1 && path[0] == '/') { //start path at index+1 ("remove" first '/')
        path++;
        path_len--;
    }
    else if (strcmp(path, "/") == 0) {
        path = "./"; // means that the path is the current directory (contains the server file)
        path_len = strlen(path);
    }
    /**3rd check: requested path does not exist*/
    if ((stat(path, &stat_buffer)) < 0) {
        send_error_response(path, NOT_FOUND, new_sockfd);
        free(buff);
        return 0;
    }
    folder_execute = folderExecutePremession(path); //check the other execute premission for every folder at the path
    if (S_ISDIR(stat_buffer.st_mode)) { //check if the path is directory
        /**4th check: path is directory but doesn't finish with '/'  */
        if (path_len >= 1 && path[path_len - 1] != '/') {
            send_error_response(path, FOUND, new_sockfd);
            free(buff);
            return 0;
        }
        if (folder_execute == INTERNAL_ERROR) {
            send_internal_error500(new_sockfd);
            free(buff);
            return 0;
        }
        if (folder_execute == NOT_FOUND) {
            send_error_response(path, NOT_FOUND, new_sockfd);
            free(buff);
            return 0;
        }
        /**5th check: path is valid directory and other has execute premission*/
        if (folder_execute == INVALID_PREMISSION) { //check for other premission to execute
            send_error_response(path, FORBIDDEN, new_sockfd);
            free(buff);
            return 0;
        }
        /**first search for index.html file and return if found and other has read premission*/
        char *path_index_html = (char *) malloc(sizeof(char) * (path_len + strlen(INDEX_FILE) + 1));
        if (path_index_html == NULL) {
            printf("malloc failed\n");
            send_internal_error500(new_sockfd);
            free(buff);
            return 0;
        }
        sprintf(path_index_html, "%s"INDEX_FILE, path);
        struct stat stat_buffer2;
        if (stat(path_index_html, &stat_buffer2) >= 0 && S_ISREG(stat_buffer2.st_mode) &&
            (stat_buffer2.st_mode & S_IROTH))
            send_file(path_index_html, &stat_buffer2, new_sockfd);
        else
            send_dir_content(path, &stat_buffer, new_sockfd);

        free(path_index_html);
        free(buff);
        return 0;
    }
    /**6th check: the file is regular, other has premission to execute all folders and read file*/
    if (folder_execute == VALID_PREMISSION && S_ISREG(stat_buffer.st_mode) && (stat_buffer.st_mode & S_IROTH))
        send_file(path, &stat_buffer, new_sockfd);
    else
        send_error_response(path, FORBIDDEN, new_sockfd);
    free(buff);
    return 0;
}


/**return VALID_PREMISSION if all folders at the path have x premission for other,INTERNAL_ERROR for malloc problem.
 *o.w return INVALID_PREMISSION*/
int folderExecutePremession(char *path) {
    struct stat st;
    if (!path)
        return INVALID_PREMISSION;
    char *pathcpy = (char *) malloc(sizeof(char) * (strlen(path) + 1));
    if (pathcpy == NULL)
        return INTERNAL_ERROR;
    char *currPath, *nextFolder = NULL;
    strcpy(pathcpy, path);
    currPath = strtok(pathcpy, "/");
    if (!currPath) {
        free(pathcpy);
        return NOT_FOUND;
    }
    while (1) {
        if (stat(currPath, &st) < 0) {
            free(pathcpy);
            return INVALID_PREMISSION;
        }
        if (S_ISDIR(st.st_mode) && !(st.st_mode & S_IXOTH)) {
            free(currPath);
            return INVALID_PREMISSION;
        }
        nextFolder = strtok(NULL, "/");
        if (!nextFolder)
            break;
        sprintf(currPath + strlen(currPath), "/%s", nextFolder);
    }
    free(currPath);
    return VALID_PREMISSION;
}

/**this method return response string contains directory content*/
void send_dir_content(char *path, struct stat *statbuf, int sockfd) {
    int response_size = dirContentSizeToAllocate(path);
    if (response_size == INTERNAL_ERROR) {
        send_internal_error500(sockfd);
        return;
    }
    DIR *dir;
    struct dirent *de;
    char *response, *pathbuf;
    char timebuf[128];
    int path_len = strlen(path);
    response = (char *) malloc(sizeof(char) * response_size);
    if (!response) {
        send_internal_error500(sockfd);
        return;
    }
    bzero(response, response_size);

    strftime(timebuf, sizeof(timebuf), RFC1123FMT, gmtime(&statbuf->st_mtime));
    construct_headers(response, 200, "OK", NULL, "text/html", -1, timebuf);
    sprintf(response + strlen(response), DIR_CONTENT_START, path, path); /**html start, table constructing..*/
    dir = opendir(path);
    while ((de = readdir(dir)) != NULL) {
        int len = strlen(de->d_name);
        int pathbuf_len = path_len + len + 1;
        pathbuf = (char *) malloc(sizeof(char) * pathbuf_len);
        if (pathbuf == NULL) {
            printf("malloc failed\n");
            closedir(dir);
            free(response);
            send_internal_error500(sockfd);
            return;
        }
        bzero(pathbuf, pathbuf_len);
        bzero(timebuf, sizeof(timebuf));
        strcpy(pathbuf, path);
        strcat(pathbuf, de->d_name);

        if (stat(pathbuf, statbuf) < 0) {
            free(pathbuf);
            continue;
        }
        /**create <td> tag for each entity*/
        strftime(timebuf, sizeof(timebuf), RFC1123FMT, gmtime(&statbuf->st_mtime));
        if (S_ISDIR(statbuf->st_mode))
            sprintf(response + strlen(response), DIR_CONTENT_FOLDER, de->d_name, de->d_name, timebuf);
        else
            sprintf(response + strlen(response), DIR_CONTENT_FILE, de->d_name, de->d_name, timebuf,
                    statbuf->st_size);
        free(pathbuf);
    }
    closedir(dir);
    sprintf(response + strlen(response), DIR_CONTENT_END, SERVER);
    if ((write(sockfd, response, strlen(response))) < 0) {
        perror("write failed");
        free(response);
        send_internal_error500(sockfd);
        return;
    }
    free(response);
    shutdown(sockfd, SHUT_RDWR);
    close(sockfd);
}

/**this functions caculate how many byes to allocate in order to build Dir content response*/
int dirContentSizeToAllocate(char *path) {
    int sum = 0, extra = 150;
    struct dirent **namelist;
    int n = scandir(path, &namelist, 0, alphasort);
    if (n < 0)
        return INTERNAL_ERROR;

    int table_td = strlen(DIR_CONTENT_FILE);
    int table_start = strlen(DIR_CONTENT_START);
    int table_end = strlen(DIR_CONTENT_END);
    int timeAndSize_len = 100;
    for (int i = 0; i < n; i++) {
        sum += (int) strlen(namelist[i]->d_name) * 2 + timeAndSize_len + table_td;
        free(namelist[i]);
    }
    free(namelist);
    sum += table_start + table_end + extra;
    return sum;
}


/**this method sends the user the wanted file if exists*/
void send_file(char *path, struct stat *statbuf, int sockfd) {
    if (!path) {
        send_internal_error500(sockfd);
        return;
    }
    int fileLength = (int)statbuf->st_size;
    char timebuf[128];
    strftime(timebuf, sizeof(timebuf), RFC1123FMT, gmtime(&statbuf->st_mtime));
    int header_len = MAX_HEADER + (int) strlen(path);
    char *header = (char *) malloc(sizeof(char) * header_len);
    if (header == NULL) {
        printf("malloc failed\n");
        send_internal_error500(sockfd);
        return;
    }
    bzero(header, header_len);
    construct_headers(header, 200, "OK", NULL, get_mime_type(path), fileLength, timebuf);
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        perror("read file failed");
        send_internal_error500(sockfd);
        free(header);
        return;
    }
    if ((send(sockfd, header, (int) strlen(header), 0) < 0)) { //send header
        perror("send failed");
        send_internal_error500(sockfd);
        free(header);
        close(fd);
        return;
    }
    free(header);
    unsigned char *file_content = (unsigned char *) malloc(sizeof(char) * MAX_READ);
    if (!file_content) {
        send_internal_error500(sockfd);
        close(fd);
        return;
    }
    int nbytes;
    while ((nbytes = (int) read(fd, file_content, MAX_READ)) > 0) {
        if ((send(sockfd, file_content, nbytes, MSG_NOSIGNAL) < 0) || nbytes < 0) {
            send_internal_error500(sockfd);
            free(file_content);
            close(fd);
            return;//error in sending
        }
        bzero(file_content, MAX_READ);
    }
    shutdown(sockfd, SHUT_RDWR);
    close(sockfd);
    free(file_content);
    close(fd);
}

/**this function construct headers (at char *res) by the given parameters*/
void
construct_headers(char *res, int status, char *title, char *location, char *mime, int length, char *last_modified) {
    time_t now;
    char timebuf[128];
    now = time(NULL);
    strftime(timebuf, sizeof(timebuf), RFC1123FMT, gmtime(&now));
    sprintf(res, "%s %d %s\r\nServer: %s\r\nDate: %s\r\n", PROTOCOL, status, title, SERVER, timebuf);
    if (location) sprintf(res + strlen(res), "Location: /%s/\r\n", location);
    if (mime) sprintf(res + strlen(res), "Content-Type: %s\r\n", mime);
    if (length >= 0) sprintf(res + strlen(res), "Content-Length: %d\r\n", length);
    if (last_modified != NULL) sprintf(res + strlen(res), "Last-Modified: %s\r\n", last_modified);
    sprintf(res + strlen(res), "Connection: close\r\n\r\n");
}

/**this function sends an error response (depending the given status)*/
void send_error_response(char *path, int status, int sockfd) {
    if (!path) {
        send_internal_error500(sockfd);
        return;
    }
    int response_size = MAX_ERROR_SIZE + (int) strlen(path);
    char *response = (char *) malloc(sizeof(char) * response_size);
    if (!response) {
        printf("malloc failed\n");
        send_internal_error500(sockfd);
        return;
    }
    bzero(response, response_size);
    char *body = (char *) malloc(sizeof(char) * (MAX_BODY_SIZE));
    if (!body) {
        printf("malloc failed\n");
        free(response);
        send_internal_error500(sockfd);
        return;
    }
    bzero(body, MAX_BODY_SIZE);

    switch (status) {
        case FOUND:
            sprintf(body, ERROR_RESPONSE_HTML, 302, "Found", 302, "Found", "Directories must end with a slash.");
            construct_headers(response, 302, "Found", path, "text/html", (int) strlen(body), NULL);
            break;
        case BAD_REQUEST:
            sprintf(body, ERROR_RESPONSE_HTML, 400, "Bad Request", 400, "Bad Request", "Bad Request.");
            construct_headers(response, 400, "Bad Request", NULL, "text/html", (int) strlen(body), NULL);
            break;
        case FORBIDDEN:
            sprintf(body, ERROR_RESPONSE_HTML, 403, "Forbidden", 403, "Forbidden", "Access denied.");
            construct_headers(response, 403, "Forbidden", NULL, "text/html", (int) strlen(body), NULL);
            break;
        case NOT_FOUND:
            sprintf(body, ERROR_RESPONSE_HTML, 404, "Not Found", 404, "Not Found", "File not found.");
            construct_headers(response, 404, "Not Found", NULL, "text/html", (int) strlen(body), NULL);
            break;
        case NOT_SUPPORTED:
            sprintf(body, ERROR_RESPONSE_HTML, 501, "Not supported", 501, "Not supported",
                    "Method is not supported.");
            construct_headers(response, 501, "Not supported", NULL, "text/html", (int) strlen(body), NULL);
            break;
        default:
            break;
    }
    strcat(response, body);
    if ((write(sockfd, response, strlen(response))) < 0) {
        perror("write failed");
        send_internal_error500(sockfd); //send internal error closing sockfd
    } else {
        shutdown(sockfd, SHUT_RDWR);
        close(sockfd);
    }
    free(response);
    free(body);
}

/**this function sends an internal error response*/
void send_internal_error500(int sockfd) {
    char *response = (char *) malloc(sizeof(char) * MAX_ERROR_SIZE);
    if (response == NULL) {
        printf("malloc failed\n");
        shutdown(sockfd, SHUT_RDWR);
        close(sockfd);
        return;
    }
    bzero(response, MAX_ERROR_SIZE);
    char *body = (char *) malloc(sizeof(char) * (MAX_BODY_SIZE));
    if (!body) {
        printf("malloc failed\n");
        free(response);
        shutdown(sockfd, SHUT_RDWR);
        close(sockfd);
        return;
    }
    bzero(body, MAX_BODY_SIZE);
    sprintf(body, ERROR_RESPONSE_HTML, 500, "Internal Server Error", 500, "Internal Server Error",
            "Some server side error.");
    construct_headers(response, 500, "Internal Server Error", NULL, "text/html", (int) strlen(body), NULL);
    strcat(response, body);
    if ((write(sockfd, response, strlen(response))) < 0)
        perror("write failed");

    shutdown(sockfd, SHUT_RDWR);
    close(sockfd);
    free(response);
    free(body);
}

char *get_mime_type(char *name) {
    if (name == NULL)
        return NULL;
    char *ext = strrchr(name, '.');
    if (!ext) return NULL;
    if (strcmp(ext, ".html") == 0 || strcmp(ext, ".htm") == 0) return "text/html";
    if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) return "image/jpeg";
    if (strcmp(ext, ".gif") == 0) return "image/gif";
    if (strcmp(ext, ".png") == 0) return "image/png";
    if (strcmp(ext, ".css") == 0) return "text/css";
    if (strcmp(ext, ".au") == 0) return "audio/basic";
    if (strcmp(ext, ".wav") == 0) return "audio/wav";
    if (strcmp(ext, ".avi") == 0) return "video/x-msvideo";
    if (strcmp(ext, ".mpeg") == 0 || strcmp(ext, ".mpg") == 0) return "video/mpeg";
    if (strcmp(ext, ".mp3") == 0) return "audio/mpeg";
    return NULL;
}

/**return 0 if str contains only digits(is a number), return -1 o.works only for positive numbers!*/
int is_a_number(char *str) {
    int i = 0;
    if (!str)
        return NOT_A_NUMBER;
    while (i < strlen(str)) {
        if (isdigit(str[i]) == 0)
            return NOT_A_NUMBER;
        i++;
    }
    return IS_A_NUMBER;
}
