/* This file is part of VoltDB.
 * Copyright (C) 2008-2014 VoltDB Inc.
 *
 * This file contains original code and/or modifications of original code.
 * Any modifications made by VoltDB Inc. are licensed under the following
 * terms and conditions:
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */
/* Copyright (C) 2008 by H-Store Project
 * Brown University
 * Massachusetts Institute of Technology
 * Yale University
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "harness.h"
#include "common/common.h"
#include "common/NValue.hpp"
#include "common/ValueFactory.hpp"
#include "common/tabletuple.h"
#include "indexes/tableindex.h"
#include "indexes/indexkey.h"
#include "indexes/CompactingTreeMultiMapIndex.h"
#include "indexes/tableindexfactory.h"
#include <ctime>
#include <cstring>

using namespace std;
using namespace voltdb;

class CompactingTreeMultiIndexTest : public Test {
public:
    CompactingTreeMultiIndexTest() {}
};

TableTuple *newTuple(TupleSchema *schema, int idx, long value) {
    TableTuple *tuple = new TableTuple(schema);
    char *data = new char[tuple->tupleLength()];
    memset(data, 0, tuple->tupleLength());
    tuple->move(data);

    tuple->setNValue(idx, ValueFactory::getBigIntValue(value));
    return tuple;
}

char* initTuples(TupleSchema *schema, int places) {
    long num = 1L << places;
    char *data = new char[25 * num];
    if (data == NULL)
        return NULL;
    memset(data, 0, 25 * num);
    for (long ii = 0; ii < num; ii++) {
        TableTuple tempTuple(data + (25 * ii), schema);
        tempTuple.setNValue(0, ValueFactory::getBigIntValue(12345));
        tempTuple.setNValue(1, ValueFactory::getBigIntValue(45688));
        tempTuple.setNValue(2, ValueFactory::getBigIntValue(rand()));
    }
    return data;
}

std::clock_t insertTuplesIntoIndex(TableIndex *index, TupleSchema *schema, char *data, int places) {
    long limit = 1L << places;
    TableTuple tempTuple(data, schema);
    std::clock_t start = std::clock();
    for (long ii = 0; ii < limit; ii++) {
    tempTuple.move(data + (25 * ii));
        index->addEntry(&tempTuple);
    }
    std::clock_t end = std::clock();
    return end - start;
}

std::clock_t insertTuplesIntoIndex2(TableIndex *index, TupleSchema *schema, char *data, int places) {
    long limit = 1L << places;
    long tmp = 1L << (places/2);
    TableTuple tempTuple(data, schema);
    std::clock_t start = std::clock();
    for (long ii = 0; ii < limit; ii++) {
    long jj = ((ii % tmp) << (places/2)) + (ii / tmp);
        tempTuple.move(data + (25 * jj));
        index->addEntry(&tempTuple);
    }
    std::clock_t end = std::clock();
    return end - start;
}

// delete num tuples
std::clock_t deleteTuplesFromIndex(TableIndex *index, TupleSchema *schema, char *data, int places, int num) {
    long gap = (1L << places) / num;
    TableTuple deleteTuple(data, schema);
    std::clock_t start = std::clock();
    for (int ii = 0; ii < num; ii++) {
    deleteTuple.move(data + (25 * gap * ii));
    index->deleteEntry(&deleteTuple);
    }
    std::clock_t end = std::clock();
    return end - start;
}

TEST_F(CompactingTreeMultiIndexTest, test1) {
    TableIndex *index = NULL;
    vector<int> columnIndices;
    vector<ValueType> columnTypes;
    vector<int32_t> columnLengths;
    vector<bool> columnAllowNull;

    columnIndices.push_back(0);
    columnTypes.push_back(VALUE_TYPE_BIGINT);
    columnLengths.push_back(NValue::getTupleStorageSize(VALUE_TYPE_BIGINT));
    columnAllowNull.push_back(false);

    TupleSchema *schema = TupleSchema::createTupleSchemaForTest(columnTypes,
                                                         columnLengths,
                                                         columnAllowNull);

    TableIndexScheme scheme("test_index", BALANCED_TREE_INDEX,
                            columnIndices, TableIndex::simplyIndexColumns(),
                            false, false, schema);
    index = TableIndexFactory::getInstance(scheme);

    TableTuple *tuple1 = newTuple(schema, 0, 10);
    index->addEntry(tuple1);
    TableTuple *tuple2 = newTuple(schema, 0, 11);
    index->addEntry(tuple2);
    TableTuple *tuple3 = newTuple(schema, 0, 12);
    index->addEntry(tuple3);

    TableTuple *tuple4 = newTuple(schema, 0, 10);
    EXPECT_TRUE(index->replaceEntryNoKeyChange(*tuple4, *tuple1));

    EXPECT_FALSE(index->exists(tuple1));
    EXPECT_TRUE(index->exists(tuple2));
    EXPECT_TRUE(index->exists(tuple3));
    EXPECT_TRUE(index->exists(tuple4));

    delete index;
    TupleSchema::freeTupleSchema(schema);
    delete[] tuple1->address();
    delete tuple1;
    delete[] tuple2->address();
    delete tuple2;
    delete[] tuple3->address();
    delete tuple3;
    delete[] tuple4->address();
    delete tuple4;
}

TEST_F(CompactingTreeMultiIndexTest, test2) {
    vector<ValueType> columnTypes;
    vector<int32_t> columnLengths;
    vector<bool> columnAllowNull;

    vector<int> columnIndices;
    vector<ValueType> kcolumnTypes;
    vector<int32_t> kcolumnLengths;
    vector<bool> kcolumnAllowNull;

    vector<int> columnIndices2;
    vector<ValueType> kcolumnTypes2;
    vector<int32_t> kcolumnLengths2;
    vector<bool> kcolumnAllowNull2;

    // tuple schema
    for(int i = 0; i < 3; i++) {
        columnTypes.push_back(VALUE_TYPE_BIGINT);
        columnLengths.push_back(NValue::getTupleStorageSize(VALUE_TYPE_BIGINT));
        columnAllowNull.push_back(false);
    }

    columnIndices.push_back(0);
    // index using one column
    kcolumnTypes.push_back(VALUE_TYPE_BIGINT);
    kcolumnLengths.push_back(NValue::getTupleStorageSize(VALUE_TYPE_BIGINT));
    kcolumnAllowNull.push_back(false);

    // index using two columns
    columnIndices2.push_back(0);
    kcolumnTypes2.push_back(VALUE_TYPE_BIGINT);
    kcolumnLengths2.push_back(NValue::getTupleStorageSize(VALUE_TYPE_BIGINT));
    kcolumnAllowNull2.push_back(false);
    columnIndices2.push_back(1);
    kcolumnTypes2.push_back(VALUE_TYPE_BIGINT);
    kcolumnLengths2.push_back(NValue::getTupleStorageSize(VALUE_TYPE_BIGINT));
    kcolumnAllowNull2.push_back(false);

    std::cout<<std::endl;

#define PLACES 16
    for (int places = PLACES; places >= 4; places -= 2 ) {
        TupleSchema *schema = TupleSchema::createTupleSchemaForTest(columnTypes,
                                                            columnLengths,
                                                            columnAllowNull);
        TupleSchema *schema1 = TupleSchema::createTupleSchemaForTest(columnTypes,
                                                            columnLengths,
                                                            columnAllowNull);
        TupleSchema *schema2 = TupleSchema::createTupleSchemaForTest(columnTypes,
                                                            columnLengths,
                                                            columnAllowNull);
        TupleSchema *kschema = TupleSchema::createTupleSchemaForTest(kcolumnTypes,
                                                             kcolumnLengths,
                                                             kcolumnAllowNull);
        TupleSchema *kschema2 = TupleSchema::createTupleSchemaForTest(kcolumnTypes2,
                                                             kcolumnLengths2,
                                                             kcolumnAllowNull2);
        TableIndexScheme scheme("test_index", BALANCED_TREE_INDEX,
                                columnIndices, TableIndex::simplyIndexColumns(),
                                false, false, schema);
        TableIndexScheme scheme1("test_index1", BALANCED_TREE_INDEX,
                                columnIndices, TableIndex::simplyIndexColumns(),
                                false, false, schema1);
        TableIndexScheme scheme2("test_index2", BALANCED_TREE_INDEX,
                                columnIndices2, TableIndex::simplyIndexColumns(),
                                false, false, schema2);
    // build index
    TableIndex *index = TableIndexFactory::getInstance(scheme);
    TableIndex *indexWithoutPointer = new CompactingTreeMultiMapIndex<IntsKey<1>, false>(kschema, scheme1);
    TableIndex *indexWithoutPointer2 = new CompactingTreeMultiMapIndex<IntsKey<2>, false>(kschema2, scheme2);
    assert(index);
    assert(indexWithoutPointer);
    assert(indexWithoutPointer2);

        char *data = initTuples(schema, places);
        EXPECT_NE(data, NULL);

    std::clock_t c1 = insertTuplesIntoIndex(index, schema, data, places);
    std::cout<<"insert 2**"<<places<< " IntsPointerKey<1> : "<< c1 <<std::endl;
    std::clock_t c2 = insertTuplesIntoIndex(indexWithoutPointer, schema, data, places);
    std::cout<<"insert 2**"<<places<< " IntsKey<1> : "<< c2 <<std::endl;
    std::clock_t c3 = insertTuplesIntoIndex(indexWithoutPointer2, schema, data, places);
    std::cout<<"insert 2**"<<places<< " IntsKey<2> : "<< c3 <<std::endl;

    c1 = deleteTuplesFromIndex(index, schema, data, places, 8);
    std::cout<<"delete 2**"<<places<< " IntsPointerKey<1> : "<< c1 <<std::endl;
    c2 = deleteTuplesFromIndex(indexWithoutPointer, schema, data, places, 8);
    std::cout<<"delete 2**"<<places<< " IntsKey<1> : "<< c2 <<std::endl;
    c3 = deleteTuplesFromIndex(indexWithoutPointer2, schema, data, places, 8);
    std::cout<<"delete 2**"<<places<< " IntsKey<2> : "<< c3 <<std::endl;
        delete data;

    // delete index
    delete index;
    delete indexWithoutPointer;
    delete indexWithoutPointer2;
        TupleSchema::freeTupleSchema(schema);
        TupleSchema::freeTupleSchema(schema1);
        TupleSchema::freeTupleSchema(schema2);
        TupleSchema::freeTupleSchema(kschema);
        TupleSchema::freeTupleSchema(kschema2);
    }

    for (int places = PLACES; places >= 4; places -= 2 ) {
        TupleSchema *schema = TupleSchema::createTupleSchemaForTest(columnTypes,
                                                            columnLengths,
                                                            columnAllowNull);
        TupleSchema *schema1 = TupleSchema::createTupleSchemaForTest(columnTypes,
                                                            columnLengths,
                                                            columnAllowNull);
        TupleSchema *schema2 = TupleSchema::createTupleSchemaForTest(columnTypes,
                                                            columnLengths,
                                                            columnAllowNull);
        TupleSchema *kschema = TupleSchema::createTupleSchemaForTest(kcolumnTypes,
                                                             kcolumnLengths,
                                                             kcolumnAllowNull);
        TupleSchema *kschema2 = TupleSchema::createTupleSchemaForTest(kcolumnTypes2,
                                                             kcolumnLengths2,
                                                             kcolumnAllowNull2);
        TableIndexScheme scheme("test_index", BALANCED_TREE_INDEX,
                                columnIndices, TableIndex::simplyIndexColumns(),
                                false, false, schema);
        TableIndexScheme scheme1("test_index1", BALANCED_TREE_INDEX,
                                columnIndices, TableIndex::simplyIndexColumns(),
                                false, false, schema1);
        TableIndexScheme scheme2("test_index2", BALANCED_TREE_INDEX,
                                columnIndices2, TableIndex::simplyIndexColumns(),
                                false, false, schema2);
    // build index
    TableIndex *index = TableIndexFactory::getInstance(scheme);
    TableIndex *indexWithoutPointer = new CompactingTreeMultiMapIndex<IntsKey<1>, false>(kschema, scheme1);
    TableIndex *indexWithoutPointer2 = new CompactingTreeMultiMapIndex<IntsKey<2>, false>(kschema2, scheme2);
    assert(index);
    assert(indexWithoutPointer);
    assert(indexWithoutPointer2);

        char *data = initTuples(schema, places);
        EXPECT_NE(data, NULL);

    std::clock_t c1 = insertTuplesIntoIndex2(index, schema, data, places);
    std::cout<<"insert 2**"<<places<< " IntsPointerKey<1> : "<< c1 <<std::endl;
    std::clock_t c2 = insertTuplesIntoIndex2(indexWithoutPointer, schema, data, places);
    std::cout<<"insert 2**"<<places<< " IntsKey<1> : "<< c2 <<std::endl;
    std::clock_t c3 = insertTuplesIntoIndex2(indexWithoutPointer2, schema, data, places);
    std::cout<<"insert 2**"<<places<< " IntsKey<2> : "<< c3 <<std::endl;

    c1 = deleteTuplesFromIndex(index, schema, data, places, 8);
    std::cout<<"delete 2**"<<places<< " IntsPointerKey<1> : "<< c1 <<std::endl;
    c2 = deleteTuplesFromIndex(indexWithoutPointer, schema, data, places, 8);
    std::cout<<"delete 2**"<<places<< " IntsKey<1> : "<< c2 <<std::endl;
    c3 = deleteTuplesFromIndex(indexWithoutPointer2, schema, data, places, 8);
    std::cout<<"delete 2**"<<places<< " IntsKey<2> : "<< c3 <<std::endl;
        delete data;

    // delete index
    delete index;
    delete indexWithoutPointer;
    delete indexWithoutPointer2;
        TupleSchema::freeTupleSchema(schema);
        TupleSchema::freeTupleSchema(schema1);
        TupleSchema::freeTupleSchema(schema2);
        TupleSchema::freeTupleSchema(kschema);
        TupleSchema::freeTupleSchema(kschema2);
    }
}

int main()
{
    return TestSuite::globalInstance()->runAll();
}
