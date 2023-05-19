#ifndef __UTILS_H__
#define __UTILS_H__

#define TRUE 1
#define FALSE 0

enum ERROR_TYPE
{
    SUCCESS,
    INFO,
    WARN,
    DEBUGGING,
    CRITIC,
};

/**
 * @brief Log messages
 *
 * @param type
 * @param message
 */
void log_message(const enum ERROR_TYPE type, const char message[], ...);

/**
 * exit printing error prefixed by `prefix` if `condition` is true (non zero)
 */
void exit_if(int condition, const char *prefix);

#endif //__UTILS_H__