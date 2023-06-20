#ifndef __REQUEST_H__

void request_handle(int fd);

int parse_request(int conn_fd, char* filename);

#endif // __REQUEST_H__
