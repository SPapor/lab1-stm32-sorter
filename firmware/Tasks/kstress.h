#ifndef KSTRESS_H
#define KSTRESS_H

/* Стрес-тест ядра, збірка TEST_KSTRESS=1 (kstress.c): створює черги, м'ютекс і
 * 4 задачі замість задачі app. Викликати до старту ядра. */
void kstress_create(void);

#endif /* KSTRESS_H */
