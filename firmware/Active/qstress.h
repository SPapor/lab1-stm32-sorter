#ifndef QSTRESS_H
#define QSTRESS_H

void qstress_start(void) __attribute__((noreturn));
void qstress_tick_hook(void);
void qstress_on_error(void);

#endif
