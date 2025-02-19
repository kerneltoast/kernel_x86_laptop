/*
 * Copyright 2024-2025 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef ISP_QUEUE_H
#define ISP_QUEUE_H

struct list_node {
	struct list_node *next;
};

struct isp_list {
	struct list_node *front;
	struct list_node *rear;
	struct mutex mutex; /* mutex for list */
	u32 count;
};

struct isp_spin_list {
	struct list_node *front;
	struct list_node *rear;
	spinlock_t lock; /* spin lock for list */
};

typedef void (*func_node_process) (struct list_node *pnode);

int isp_list_init(struct isp_list *list);
int isp_list_destory(struct isp_list *list, func_node_process func);
int isp_list_insert_tail(struct isp_list *list, struct list_node *node);
void isp_list_rm_node(struct isp_list *list, struct list_node *node);
struct list_node *isp_list_get_first(struct isp_list *list);
struct list_node *isp_list_get_first_without_rm(struct isp_list *list);
u32 isp_list_get_cnt(struct isp_list *list);

int isp_spin_list_init(struct isp_spin_list *list);
int isp_spin_list_destroy(struct isp_spin_list *list,
			  func_node_process func);
int isp_spin_list_insert_tail(struct isp_spin_list *list,
			      struct list_node *node);
void isp_spin_list_rm_node(struct isp_spin_list *list, struct list_node *node);
struct list_node *isp_spin_list_get_first(struct isp_spin_list *list);

u32 isp_spin_list_get_cnt(struct isp_spin_list *list);

int isp_spin_list_destory(struct isp_spin_list *list,
			  func_node_process func);
#endif
