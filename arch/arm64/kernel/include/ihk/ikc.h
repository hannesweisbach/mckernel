/* ikc.h COPYRIGHT FUJITSU LIMITED 2015 */
#ifndef __HEADER_ARM64_IHK_IKC_H
#define __HEADER_ARM64_IHK_IKC_H

#include <ikc/ihk.h>

#define IKC_PORT_IKC2MCKERNEL 501
#define IKC_PORT_IKC2LINUX    503

/* manycore side */
int ihk_mc_ikc_init_first(struct ihk_ikc_channel_desc *,
                          ihk_ikc_ph_t handler);

#endif /* !__HEADER_ARM64_IHK_IKC_H */
