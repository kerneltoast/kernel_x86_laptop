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

#include "isp_common.h"
#include "isp_queue.h"
#include "isp_debug.h"

int isp_list_init(struct isp_list *list)
{
	if (!list)
		return -EINVAL;
	mutex_init(&list->mutex);
	list->front = kmalloc(sizeof(*list->front), GFP_KERNEL);
	if (IS_ERR_OR_NULL(list->front))
		return PTR_ERR(list->front);

	list->rear = list->front;
	list->front->next = NULL;
	list->count = 0;
	return 0;
}

int isp_list_destory(struct isp_list *list, func_node_process func)
{
	struct list_node *p;

	if (!list)
		return -EINVAL;
	mutex_lock(&list->mutex);
	while (list->front != list->rear) {
		p = list->front->next;
		list->front->next = p->next;
		if (list->rear == p)
			list->rear = list->front;
		if (func)
			func(p);
	}
	kfree(list->front);
	list->front = NULL;
	list->rear = NULL;
	mutex_unlock(&list->mutex);

	mutex_destroy(&list->mutex);

	return 0;
};

int isp_list_insert_tail(struct isp_list *list, struct list_node *p)
{
	if (!list)
		return -EINVAL;
	mutex_lock(&list->mutex);

	list->rear->next = p;
	list->rear = p;
	p->next = NULL;
	list->count++;
	mutex_unlock(&list->mutex);
	return 0;
}

struct list_node *isp_list_get_first(struct isp_list *list)
{
	struct list_node *p;

	if (!list)
		return NULL;

	mutex_lock(&list->mutex);
	if (list->front == list->rear) {
		if (list->count)
			pr_err("fail bad count %u",
			       list->count);
		mutex_unlock(&list->mutex);
		return NULL;
	}

	p = list->front->next;
	list->front->next = p->next;
	if (list->rear == p)
		list->rear = list->front;
	if (list->count)
		list->count--;
	else
		pr_warn("fail bad 0 count");
	mutex_unlock(&list->mutex);
	return p;
}

struct list_node *isp_list_get_first_without_rm(struct isp_list *list)
{
	struct list_node *p;

	if (!list)
		return NULL;

	mutex_lock(&list->mutex);
	if (list->front == list->rear) {
		mutex_unlock(&list->mutex);
		return NULL;
	}

	p = list->front->next;
	mutex_unlock(&list->mutex);
	return p;
}
