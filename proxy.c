
#include <stdio.h>
#include "csapp.h"
/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

typedef struct node {
    char key[MAXLINE];
    char *data;
    int size;
    struct node *next;
    struct node *prev;
} node_t;

node_t *head = NULL;
node_t *tail = NULL;
int total = 0;
pthread_mutex_t lock;

void remove_node(node_t *p)
{
    if (p->prev != NULL)
        p->prev->next = p->next;
    else
        head = p->next;
    if (p->next != NULL)
        p->next->prev = p->prev;
    else
        tail = p->prev;
    total -= p->size;
    Free(p->data);
    Free(p);
}

char *find_cache(char *key, int *size)
{
    node_t *p;
    char *copy = NULL;

    pthread_mutex_lock(&lock);
    for (p = head; p != NULL; p = p->next) {
        if (strcmp(p->key, key) == 0) {
            if (p != head) {
                if (p->prev != NULL)
                    p->prev->next = p->next;
                if (p->next != NULL)
                    p->next->prev = p->prev;
                else
                    tail = p->prev;
                p->prev = NULL;
                p->next = head;
                head->prev = p;
                head = p;
            }
            copy = Malloc(p->size);
            memcpy(copy, p->data, p->size);
            *size = p->size;
            break;
        }
    }
    pthread_mutex_unlock(&lock);
    return copy;
}

void add_cache(char *key, char *data, int size)
{
    node_t *p;

    if (size <= 0 || size > MAX_OBJECT_SIZE)
        return;

    p = Malloc(sizeof(node_t));
    strcpy(p->key, key);
    p->data = Malloc(size);
    memcpy(p->data, data, size);
    p->size = size;

    pthread_mutex_lock(&lock);
    p->prev = NULL;
    p->next = head;
    if (head != NULL)
        head->prev = p;
    else
        tail = p;
    head = p;
    total += size;
    while (total > MAX_CACHE_SIZE) {
        remove_node(tail);
    }
    pthread_mutex_unlock(&lock);
}

void doit(int fd)
{
    char buf[MAXLINE], method[MAXLINE], url[MAXLINE], version[MAXLINE];
    char line[MAXLINE];
    char headers[MAXLINE * 4];
    char host[MAXLINE], path[MAXLINE], port[16];
    char key[MAXLINE];
    char response[MAXLINE * 4];
    char *rest, *slash, *colon;
    char *body, *cached;
    int fd2, size, clen;
    rio_t rio;

    rio_readinitb(&rio, fd);
    if (rio_readlineb(&rio, buf, MAXLINE) <= 0)
        return;
    sscanf(buf, "%s %s %s", method, url, version);
    if (strcasecmp(method, "GET") != 0)
        return;

    headers[0] = '\0';
    while (rio_readlineb(&rio, line, MAXLINE) > 0) {


        
        if (strcmp(line, "\r\n") == 0)
            break;
        if (strncmp(line, "User-Agent:", 11) != 0) {
            strcat(headers, line);
        }
    }

    rest = strstr(url, "//");
    if (rest == NULL)
        rest = url;
    else
        rest = rest + 2;

    slash = strchr(rest, '/');
    if (slash == NULL)
        strcpy(path, "/");
    else {
        strcpy(path, slash);
        *slash = '\0';
    }

    colon = strchr(rest, ':');
    if (colon == NULL) {
        strcpy(host, rest);
        strcpy(port, "80");
    }
    else {
        *colon = '\0';
        strcpy(host, rest);
        strcpy(port, colon + 1);
    }

    sprintf(key, "%s:%s%s", host, port, path);

    cached = find_cache(key, &clen);
    if (cached != NULL) {
        rio_writen(fd, cached, clen);
        Free(cached);
        return;
    }

    fd2 = Open_clientfd(host, port);

    sprintf(buf, "%s %s %s\r\n", method, path, version);
    rio_writen(fd2, buf, strlen(buf));
    rio_writen(fd2, headers, strlen(headers));
    rio_writen(fd2, user_agent_hdr, strlen(user_agent_hdr));
    rio_writen(fd2, "\r\n", 2);

    rio_readinitb(&rio, fd2);
    response[0] = '\0';
    if (rio_readlineb(&rio, line, MAXLINE) <= 0) {
        Close(fd2);
        return;
    }
    strcat(response, line);

    size = 0;
    while (rio_readlineb(&rio, line, MAXLINE) > 0) {
        strcat(response, line);
        if (strcmp(line, "\r\n") == 0)
            break;
        if (strstr(line, "Content-length:") != NULL) {
            colon = strchr(line, ':');
            size = atoi(colon + 1);
        }
    }

    body = Malloc(size);
    rio_readnb(&rio, body, size);
    Close(fd2);

    int len = strlen(response);
    char *all = Malloc(len + size);
    memcpy(all, response, len);
    memcpy(all + len, body, size);

    rio_writen(fd, all, len + size);

    add_cache(key, all, len + size);
    Free(body);
    Free(all);
}

void *thread(void *vargp)
{
    int connfd = *(int *)vargp;
    pthread_detach(pthread_self());
    Free(vargp);
    doit(connfd);
    Close(connfd);
    return NULL;
}

int main(int argc, char **argv)
{
    int listenfd, connfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid;

    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    signal(SIGPIPE, SIG_IGN);
    pthread_mutex_init(&lock, NULL);

    listenfd = Open_listenfd(argv[1]);
    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        int *p = Malloc(sizeof(int));
        *p = connfd;
        Pthread_create(&tid, NULL, thread, p);
    }
    return 0;
}
