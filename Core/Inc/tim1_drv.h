/*
 * tim1_drv.h
 *
 *  Created on: Jul 23, 2026
 *      Author: domenico
 */

#ifndef INC_TIM1_DRV_H_
#define INC_TIM1_DRV_H_

typedef struct _tag_dbg
{
	uint16_t id;
	uint16_t type;
	uint32_t time2;
	uint16_t time1;
	uint16_t ovf;
	uint64_t time1_ovf;
	uint64_t period;
	uint32_t backlog;
}DBG;

#define DBG_SIZE	(1024U)
#define DBG_MASK 	(DBG_SIZE - 1)

typedef struct
{
	volatile uint32_t wr;
	volatile uint32_t rd;

	volatile uint32_t max_pending;
	volatile uint32_t overflow_cnt;

	DBG buffer[DBG_SIZE];

} DBG_RING;

extern uint16_t tim1_drv_get_value	(uint64_t *val);
extern uint16_t tim1_drv_get_dbg	(DBG * dbg);
extern uint16_t dbg_pop				(DBG *item);
extern uint32_t dbg_count			(void);
extern uint32_t dbg_get_overflow	(void);

#endif /* INC_TIM1_DRV_H_ */
