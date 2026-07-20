#ifndef IWDG_PRIVATE_H
#define IWDG_PRIVATE_H

#include "std_types.h"

typedef struct {
    volatile uint32 KR;   /* 0x00 - Key register       */
    volatile uint32 PR;   /* 0x04 - Prescaler register  */
    volatile uint32 RLR;  /* 0x08 - Reload register     */
    volatile uint32 SR;   /* 0x0C - Status register     */
} IwdgType;

#define IWDG_BASE_ADDR   0x40003000UL
#define IWDG             ((IwdgType *) IWDG_BASE_ADDR)

/* SR bit positions */
#define IWDG_SR_PVU   0U   /* Prescaler value update in progress */
#define IWDG_SR_RVU   1U   /* Reload value update in progress    */

#endif