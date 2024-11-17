/*
 * PROJECT:     ReactOS Setup Library
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Linked-list support macros
 * COPYRIGHT:   Copyright 2005-2018 ReactOS Team
 *              Copyright 2023-2024 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 */

#pragma once

/**
 * @brief
 * Inserts an entry of a given type into a linked-list, keeping it
 * sorted in ascending order according to the value of a given field.
 **/
#define InsertAscendingList(ListHead, NewEntry, Type, ListEntryField, SortField) \
do { \
    PLIST_ENTRY current = (ListHead)->Flink; \
    while (current != (ListHead)) \
    { \
        if (CONTAINING_RECORD(current, Type, ListEntryField)->SortField >= \
            (NewEntry)->SortField) \
        { \
            break; \
        } \
        current = current->Flink; \
    } \
\
    InsertTailList(current, &((NewEntry)->ListEntryField)); \
} while (0)


/**
 * @brief
 * Retrieves a pointer to a linked-list entry, that is
 * adjacent (next or previous) to the given one.
 *
 * @param[in]   ListEntry
 * The entry for which to retrieve the adjacent entry.
 *
 * @param[in]   Direction
 * TRUE or FALSE to retrieve the next or previous entry, respectively.
 **/
#define ADJ_LIST_ENTRY(ListEntry, Direction) \
    ((Direction) ? (ListEntry)->Flink : (ListEntry)->Blink)

/**
 * @brief
 * Retrieves the containing structure (record) that is adjacent
 * to the given one.
 *
 * @param[out]  pAdjRecord
 * Retrieves a pointer to the adjacent record, if any, or NULL if none.
 *
 * @param[in]   ListHead
 * Head of the list containing the records.
 *
 * @param[in]   Record
 * The current record for which to retrieve the adjacent one,
 * or NULL to start from the head of the list.
 *
 * @param[in]   RecordType
 * The type name of the structure of each record stored in the list.
 *
 * @param[in]   Field
 * The name of the LIST_ENTRY field which is contained in
 * a structure of type RecordType.
 *
 * @param[in]   Direction
 * TRUE or FALSE to retrieve the next or previous record, respectively.
 **/
#define GET_ADJ_RECORD_IMPL(pAdjRecord, ListHead, Record, RecordType, Field, Direction) \
do { \
    const LIST_ENTRY* _ListEntry = ((Record) ? &(Record)->Field : (ListHead)); \
    _ListEntry = ADJ_LIST_ENTRY(_ListEntry, (Direction)); \
    (pAdjRecord) = ((_ListEntry == (ListHead)) \
        ? NULL : CONTAINING_RECORD(_ListEntry, RecordType, Field)); \
} while (0)


#define GET_ADJ_RECORD_DEF_(Name, RecordType, Field) \
RecordType* \
Name( \
    _In_ const LIST_ENTRY* ListHead, \
    _In_opt_ const RecordType* Record, \
    _In_ BOOLEAN Direction) \
{ \
    GET_ADJ_RECORD_IMPL(Record, ListHead, Record, RecordType, Field, Direction); \
    return (RecordType*)Record; \
}

#define GET_ADJ_RECORD_DEF(RecordType, Field) \
    GET_ADJ_RECORD_DEF_(GET_ADJ_RECORD_ ## RecordType, RecordType, Field)

/* EOF */
