#ifndef XV6_VF2_GPIO_H
#define XV6_VF2_GPIO_H

void vf2_gpio_init(void);
int vf2_gpio_output(int gpio);
int vf2_gpio_write(int gpio, int value);

#endif