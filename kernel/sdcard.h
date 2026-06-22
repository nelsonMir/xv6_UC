struct buf;

void sd_init(void);
void sd_rw(struct buf *b, int write);