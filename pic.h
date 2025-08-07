#pragma once
void pic_remap();
void pic_send_eoi(unsigned char irq);
void pic_mask_irq(unsigned char irq);
void pic_unmask_irq(unsigned char irq);
void pic_unmask_irq1(); 