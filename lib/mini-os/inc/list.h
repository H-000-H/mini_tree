/**
 * @copyright SPDX-License-Identifier: Apache-2.0
 * @author H-000-H
 * @file list.h
 * @brief mini-os list implementation
 */
#ifndef LIST_H
#define LIST_H

#include "err.h"
#include "mini_config.h"
#include "redef.h"
#if defined(__cplusplus)
extern "C"
{
#endif
// clang-format off
typedef struct mini_os_list_node mini_os_list_t;
/**
 * @brief mini-os Doubly linked list node
 */
struct mini_os_list_node
{
    struct mini_os_list_node* next;         /**< Next node in the list */
    struct mini_os_list_node* prev;         /**< Previous node in the list */
};

typedef struct mini_os_single_list_node mini_os_single_list_t;
/**
 * @brief mini-os single list node
 */
struct mini_os_single_list_node
{
    struct mini_os_single_list_node* next;  /**< Next node in the list */
};
// clang-format on
/*---------------------------------------------------------------------------------------------------------*/
/*                                    double linked list functions */
/*---------------------------------------------------------------------------------------------------------*/
/**
 * @brief Initialize a mini-os list
 * @param[in] list_node The list to initialize
 * @return MINI_OS_OK on success, negative error code on failure
 */
MINI_OS_STATIC_INLINE mini_os_err_t mini_os_list_init(mini_os_list_t* list_node)
{
    if (list_node == MINI_OS_NULL)
        return MINI_OS_ERR_INVAL;
    list_node->next = list_node;
    list_node->prev = list_node;
    return MINI_OS_OK;
}

/**
 * @brief Add a node between next and prev nodes in a mini-os list
 * @param[in] next The next node in the list
 * @param[in] prev The previous node in the list
 * @param[in] node The node to add
 * @return MINI_OS_OK on success, negative error code on failure
 */
MINI_OS_STATIC_INLINE mini_os_err_t mini_os_list_add(struct mini_os_list_node* next, struct mini_os_list_node* prev, struct mini_os_list_node* node)
{
    if (next == MINI_OS_NULL || prev == MINI_OS_NULL || node == MINI_OS_NULL)
        return MINI_OS_ERR_INVAL;
    node->next = next;
    node->prev = prev;
    next->prev = node;
    prev->next = node;
    return MINI_OS_OK;
}

/**
 * @brief Add a node to the tail of a mini-os list
 * @param[in] new_node The node to add
 * @param[in] head The head of the list
 * @return MINI_OS_OK on success, negative error code on failure
 */
MINI_OS_STATIC_INLINE mini_os_err_t mini_os_list_tail(struct mini_os_list_node* new_node, struct mini_os_list_node* head)
{
    if (new_node == MINI_OS_NULL || head == MINI_OS_NULL)
        return MINI_OS_ERR_INVAL;
    return mini_os_list_add(head, head->prev, new_node);
}

/**
 * @brief Add a node to the head of a mini-os list (before its current first node)
 * @param[in] new_node The node to add
 * @param[in] head The head of the list
 * @return MINI_OS_OK on success, negative error code on failure
 * @note do NOT open-code this as mini_os_list_add(head, head, new_node):
 *       handing the sentinel twice as (next, prev) re-points the sentinel at
 *       the new node alone and orphans every node already queued on the list
 */
MINI_OS_STATIC_INLINE mini_os_err_t mini_os_list_head(struct mini_os_list_node* new_node, struct mini_os_list_node* head)
{
    if (new_node == MINI_OS_NULL || head == MINI_OS_NULL)
        return MINI_OS_ERR_INVAL;
    return mini_os_list_add(head->next, head, new_node);
}

/**
 * @brief Remove a node from a mini-os list
 * @param[in] node The node to remove
 * @return MINI_OS_OK on success, negative error code on failure
 */
MINI_OS_STATIC_INLINE mini_os_err_t mini_os_list_remove(struct mini_os_list_node* node)
{
    if (node == MINI_OS_NULL)
        return MINI_OS_ERR_INVAL;
    node->next->prev = node->prev;
    node->prev->next = node->next;
    node->next = node;
    node->prev = node;
    return MINI_OS_OK;
}

/**
 * @brief Check if a mini-os list is empty
 * @param[in] list_node The list to check
 * @return MINI_OS_TRUE if the list is empty, MINI_OS_FALSE otherwise
 */
MINI_OS_STATIC_INLINE mini_os_bool_t mini_os_list_is_empty(struct mini_os_list_node* list_node)
{
    if (list_node == MINI_OS_NULL)
        return MINI_OS_FALSE;
    return (list_node->next == list_node);
}

/*---------------------------------------------------------------------------------------------------------*/
/*                                    single linked list functions */
/*---------------------------------------------------------------------------------------------------------*/
/**
 * @brief Initialize a mini-os single list node
 * @param[in] node The node to initialize
 * @return MINI_OS_OK on success, MINI_OS_ERR_INVAL if node is NULL
 */
MINI_OS_STATIC_INLINE mini_os_err_t mini_os_single_list_init(struct mini_os_single_list_node* node)
{
    if (node == MINI_OS_NULL)
        return MINI_OS_ERR_INVAL;
    node->next = node;
    return MINI_OS_OK;
}

/**
 * @brief Add a node to a mini-os single list
 * @param[in] next The next node in the list
 * @param[in] prev The previous node in the list
 * @param[in] node The node to add
 * @return MINI_OS_OK on success, MINI_OS_ERR_INVAL if any parameter is NULL
 */
MINI_OS_STATIC_INLINE mini_os_err_t mini_os_single_list_add(struct mini_os_single_list_node* next, struct mini_os_single_list_node* prev, struct mini_os_single_list_node* node)
{
    if (next == MINI_OS_NULL || prev == MINI_OS_NULL || node == MINI_OS_NULL)
        return MINI_OS_ERR_INVAL;
    node->next = next;
    prev->next = node;
    return MINI_OS_OK;
}

/**
 * @brief Add a node to a mini-os single list before a given node
 * @param[in] prev The previous node in the list
 * @param[in] node The node to add
 * @return MINI_OS_OK on success, MINI_OS_ERR_INVAL if any parameter is NULL
 */
MINI_OS_STATIC_INLINE mini_os_err_t mini_os_single_list_add_after(struct mini_os_single_list_node* prev, struct mini_os_single_list_node* node) { return mini_os_single_list_add(prev->next, prev, node); }

/**
 * @brief Push a node to the heap of a mini-os single list
 * @param[in] heap The heap of the list
 * @param[in] node The node to push
 * @return MINI_OS_OK on success, MINI_OS_ERR_INVAL if any parameter is NULL
 */
MINI_OS_STATIC_INLINE mini_os_err_t mini_os_single_list_push_heap(struct mini_os_single_list_node* heap, struct mini_os_single_list_node* node) { return mini_os_single_list_add_after(heap, node); }
/**
 * @brief Remove a node from a mini-os single list
 * @param[in] node The node to remove
 * @param[in] prev The previous node in the list
 * @return MINI_OS_OK on success, MINI_OS_ERR_INVAL if any parameter is NULL
 */
MINI_OS_STATIC_INLINE mini_os_err_t mini_os_single_list_remove(struct mini_os_single_list_node* node, struct mini_os_single_list_node* prev)
{
    if (node == MINI_OS_NULL || prev == MINI_OS_NULL)
        return MINI_OS_ERR_INVAL;
    prev->next = node->next;
    node->next = node;
    return MINI_OS_OK;
}

/**
 * @brief Check if a mini-os single list is empty
 * @param[in] node The node to check
 * @return MINI_OS_TRUE if the list is empty, MINI_OS_FALSE otherwise
 */
MINI_OS_STATIC_INLINE mini_os_bool_t mini_os_single_list_is_empty(struct mini_os_single_list_node* node)
{
    if (node == MINI_OS_NULL)
        return MINI_OS_FALSE;
    return (node->next == node) ? MINI_OS_TRUE : MINI_OS_FALSE;
}

#if defined(__cplusplus)
}
#endif

#endif
