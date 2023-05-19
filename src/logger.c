#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <time.h>

#define ANSI_COLOR_RED "\x1b[31m"
#define ANSI_COLOR_GREEN "\x1b[32m"
#define ANSI_COLOR_YELLOW "\x1b[33m"
#define ANSI_COLOR_BLUE "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN "\x1b[36m"
#define ANSI_COLOR_RESET "\x1b[0m"

#include "logger.h"

void log_message(const enum ERROR_TYPE type, const char *message, ...)
{
    va_list args;
    va_start(args, message);
    // FILE *file = fopen("server.log", "a");
    char *error_message = "Info";
    switch (type)
    {
    case DEBUGGING:
    {
        error_message = "Debug";
        fprintf(stderr, ANSI_COLOR_MAGENTA);
        break;
    }
    case WARN:
    {
        error_message = "Warning";
        fprintf(stderr, ANSI_COLOR_YELLOW);
        break;
    }
    case CRITIC:
    {
        error_message = "Critic";
        fprintf(stderr, ANSI_COLOR_RED);
        fprintf(stderr, "NOK : ");
        vfprintf(stderr, message, args);
        fprintf(stderr, "\n");
        break;
    }
    case SUCCESS:
    {
        error_message = "Success";
        fprintf(stderr, ANSI_COLOR_GREEN);
        break;
    }
    case INFO:
    {
        fprintf(stderr, ANSI_COLOR_BLUE);
        break;
    }
    default:
        fprintf(stderr, ANSI_COLOR_RESET);
    }

    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char timestamp[20];
    strftime(timestamp, sizeof(timestamp), "%d-%m-%Y %H:%M:%S", t);

    fprintf(stderr, "%s %s", timestamp, error_message);

    fprintf(stderr, ANSI_COLOR_RESET);
    fprintf(stderr, " : ");

    vfprintf(stderr, message, args);
    fprintf(stderr, "\n");
    va_end(args);

    fclose(stderr);
}

void exit_if(int condition, const char *prefix)
{
    if (condition)
    {
        if (errno != 0)
        {
            log_message(CRITIC, "%s -> %s", prefix, strerror(errno));
            perror(prefix);
        }
        else
        {
            log_message(CRITIC, prefix);
        }
        exit(EXIT_FAILURE);
    }
}