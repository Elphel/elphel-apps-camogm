/** @file index_list.c
 * @brief Provides data structures and functions for working with disk index directory
 * @copyright  Copyright (C) 2016 Elphel, Inc.
 *
 * <b>License:</b>
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

#include "index_list.h"

/**
 * @brief Create a new node in a linked list of disk indexes
 * @param[in,out]   index   pointer to a newly allocated structure
 * @return          0 in case a new disk index structure was successfully created and -1 otherwise
 */
int create_node(struct disk_index **index)
{
	if (*index != NULL)
		return -1;

	*index = malloc(sizeof(struct disk_index));
	if (*index != NULL) {
		memset(*index, 0, sizeof(struct disk_index));
		return 0;
	} else {
		return -1;
	}
}

/**
 * @brief Add a new index node to disk index directory
 * @param[in,out]   idir   pointer to disk index directory
 * @param[in]       index  pointer to index node to be added to disk index directory
 * @return          The number of entries in disk index directory
 */
int add_node(struct disk_idir *idir, struct disk_index *index)
{
	if (idir->head == NULL && idir->tail == NULL) {
		idir->head = index;
		idir->tail = index;
		idir->size = 1;
	} else {
		index->prev = idir->tail;
		idir->tail->next = index;
		idir->tail = index;
		idir->size++;
	}

	return idir->size;
}

/**
 * @brief Insert new node in chronological order
 * @param[in,out]   idir   index directory to which a new node should be added
 * @param[in]       indx   a pointer to new node
 * @return          The number of nodes in the directory
 */
int insert_node(struct disk_idir *idir, struct disk_index *indx)
{
	int ret = 0;
	struct disk_index *node;

	if (idir->head == NULL) {
		add_node(idir, indx);
		return 1;
	}

	node = idir->head;
	while (node != NULL) {
		if (indx->rawtime < node->rawtime) {
			ret = insert_prev(idir, node, indx);
			break;
		}
		node = node->next;
	}
	if (node == NULL)
		ret = insert_next(idir, idir->tail, indx);

	return ret;
}

/**
 * @brief Insert new index to the list before the index specified. It means that new index will
 * be inserted toward the head.
 * @param[in,out]   idir   pointer to the lined list of indexes
 * @param[in]       parent pointer to the element before which the new element will be inserted
 * @param[in]       new_indx pointer to the element which will be inserted
 * @return          The number of nodes in the list
 */
int insert_prev(struct disk_idir *idir, struct disk_index *parent, struct disk_index *new_indx)
{
	if (parent->prev != NULL) {
		new_indx->next = parent;
		new_indx->prev = parent->prev;
		parent->prev->next = new_indx;
		parent->prev = new_indx;
	} else {
		new_indx->next = parent;
		new_indx->prev = NULL;
		parent->prev = new_indx;
		idir->head = new_indx;
	}
	idir->size++;

	return idir->size;
}
/**
 * @brief Insert new index to the list after the index specified. It means that new index will
 * be inserted toward the tail.
 * @param[in,out]   idir   pointer to the linked list of indexes
 * @param[in]       parent pointer to the element after which the new element will be inserted
 * @param[in]       new_indx pointer to the element which will be inserted
 * @return          The number of nodes in the list
 */
int insert_next(struct disk_idir *idir, struct disk_index *parent, struct disk_index *new_indx)
{
	if (parent->next != NULL) {
		new_indx->next = parent->next;
		new_indx->prev = parent;
		parent->next->prev = new_indx;
		parent->next = new_indx;
	} else {
		new_indx->next = NULL;
		new_indx->prev = parent;
		parent->next = new_indx;
		idir->tail = new_indx;
	}
	idir->size++;

	return idir->size;
}

/**
 * @brief Find index node by its start offset
 * @param[in]   idir   pointer to disk index directory
 * @param[in]   offset the offset of the file which should be found
 * @return      pointer to disk index node or NULL if the corresponding file was not found
 */
struct disk_index *find_by_offset(const struct disk_idir *idir, uint64_t offset)
{
	struct disk_index *index = idir->head;

	while (index != NULL) {
		if (index->f_offset == offset)
			break;
		index = index->next;
	}

	return index;
}

/** @brief Find index node by its time stamp
 * @param[in]   idir   pointer to disk index directory
 * @param[in]   time   the time stamp of the file which should be found
 * @return      pointer to disk index node or NULL if the corresponding file was not found
 */
struct disk_index *find_nearest_by_time(const struct disk_idir *idir, time_t time)
{
	struct disk_index *ptr = NULL;
	struct disk_index *index = idir->head;

	if (idir->size == 0)
		return NULL;
	if (index->next != NULL)
		ptr = index->next;
	else
		return index;

	while (index != NULL) {
		if (fabs(difftime(index->rawtime, time)) < fabs(difftime(ptr->rawtime, time)))
			ptr = index;
		index = index->next;
	}

	return ptr;
}

/**
 * @brief Remove a single index node from disk index directory
 * @param[in,out]   idir   pointer to disk index directory
 * @param[in]       node   pointer to the index node which should be removed
 * @return          The number of entries in disk index directory
 */
int remove_node(struct disk_idir *idir, struct disk_index *node)
{
	if (node == NULL)
		return -1;

	if (node == idir->head) {
		idir->head = node->next;
		idir->head->prev = NULL;
	} else if (node == idir->tail) {
		idir->tail = node->prev;
		idir->tail->next = NULL;
	} else {
		struct disk_index *ind = idir->head;
		while (ind != NULL) {
			if (ind == node) {
				ind->prev->next = ind->next;
				ind->next->prev = ind->prev;
				break;
			}
			ind = ind->next;
		}
	}
	free(node);
	node = NULL;
	idir->size--;

	return idir->size;
}

/**
 * @brief Remove all entries from disk index directory an free memory
 * @param[in]   idir   pointer to disk index directory
 * @return      0 in case the directory was successfully deleted and -1 if the directory was empty
 */
int delete_idir(struct disk_idir *idir)
{
	struct disk_index *curr_ind;
	struct disk_index *next_ind;

	if (idir == NULL || idir->head == NULL)
		return -1;

	curr_ind = idir->head;
	next_ind = curr_ind->next;
	while (curr_ind != NULL) {
		free(curr_ind);
		curr_ind = next_ind;
		if (curr_ind != NULL)
			next_ind = curr_ind->next;
	}
	idir->head = idir->tail = NULL;
	idir->size = 0;

	return 0;
}
