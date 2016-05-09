/*
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package org.apache.spark.sql.execution.datasources.spinach

import org.apache.hadoop.conf.Configuration
import org.apache.hadoop.mapreduce.{JobID, TaskAttemptContext, TaskAttemptID, TaskID}
import org.apache.hadoop.mapreduce.task.TaskAttemptContextImpl
import org.apache.spark.sql.catalyst.InternalRow
import org.apache.spark.sql.catalyst.expressions.Ascending
import org.apache.spark.sql.sources.{And, EqualTo, Filter, GreaterThan, GreaterThanOrEqual, LessThan, LessThanOrEqual}
import org.apache.spark.sql.types.{IntegerType, StringType, StructType}
import org.apache.spark.{Logging, SparkFunSuite}
import org.scalatest.BeforeAndAfterAll

private[spinach] class IntValues(values: Array[Int]) extends IndexNodeValue {
  override def length: Int = values.length
  override def apply(idx: Int): Int = values(idx)
}

private[spinach] class NonLeafNode(
    keys: Array[Key],
    children: Array[IndexNode]) extends IndexNode {
  assert(keys.length + 1 == children.length)
  override def length: Int = keys.length
  override def next: IndexNode = throw new NotImplementedError("")
  override def childAt(idx: Int): IndexNode = children(idx)
  override def isLeaf: Boolean = false
  override def keyAt(idx: Int): Key = keys(idx)
  override def valueAt(idx: Int): IndexNodeValue = throw new NotImplementedError("")
}

private[spinach] class LeafNode(keys: Array[Key], values: Array[IntValues], sibling: IndexNode)
    extends IndexNode {
  override def length: Int = keys.length
  override def next: IndexNode = sibling
  override def childAt(idx: Int): IndexNode = throw new NotImplementedError("")
  override def isLeaf: Boolean = true
  override def keyAt(idx: Int): Key = keys(idx)
  override def valueAt(idx: Int): IndexNodeValue = values(idx)
}

private[spinach] class BPlusTreeSearchSuite
    extends SparkFunSuite with Logging with BeforeAndAfterAll {
  implicit def int2internalRow(keys: Array[Int]): Array[Key] = keys.map(InternalRow(_))
  val attemptContext: TaskAttemptContext = new TaskAttemptContextImpl(
    new Configuration(),
    new TaskAttemptID(new TaskID(new JobID(), true, 0), 0))

  // The data looks like:
  //                      5             10             15     <-----Root Key
  //                      |              |              |
  //            (3, 4, 5) -> (8, 9, 10) -> (13, 14, 15) -> (16, 17, 18)    <--- Second Level Key
  //             |  |  |     |  |  |        |   |   |       |   |   |
  //            30 40  50   80 90  100     130 140 150     160  170 180    <--- Values
  //            31 41       81 91          131 141         161  171
  //            32          82             132             162
  //
  val i14 = new LeafNode(
    Array(16, 17, 18),
    Array(new IntValues(Array(160, 161, 162)),
          new IntValues(Array(170, 171)),
          new IntValues(Array(180))),
    null)

  val i13 = new LeafNode(
    Array(13, 14, 15),
    Array(new IntValues(Array(130, 131, 132)),
      new IntValues(Array(140, 141)),
      new IntValues(Array(150))),
    i14)

  val i12 = new LeafNode(
    Array(8, 9, 10),
    Array(new IntValues(Array(80, 81, 82)),
      new IntValues(Array(90, 91)),
      new IntValues(Array(100))),
    i13)

  val i11 = new LeafNode(
    Array(3, 4, 5),
    Array(new IntValues(Array(30, 31, 32)),
      new IntValues(Array(40, 41)),
      new IntValues(Array(50))),
    i12)

  val root = new NonLeafNode(Array(5, 10, 15), Array(i11, i12, i13, i14))

  val indexMeta: IndexMeta = new IndexMeta("test", BTreeIndex(BTreeIndexEntry(1) :: Nil)) {
    override def open(context: TaskAttemptContext): IndexNode = root
  }

  val meta = new DataSourceMeta(
    null,
    Array(indexMeta),
    new StructType().add("fake", StringType, true).add("test", IntegerType, true),
    null)

  test("equal 11") {
    val filters: Array[Filter] = Array(EqualTo("test", 11))
    assertScanner(meta, filters, None, Set.empty[Int])
  }

  test("equal 10") {
    val filters: Array[Filter] = Array(EqualTo("test", 10))
    assertScanner(meta, filters, None, Set(100))
  }

  test("> 15") {
    val filters: Array[Filter] = Array(GreaterThan("test", 15))
    assertScanner(meta, filters, None, Set(160, 161, 162, 170, 171, 180))
  }

  test(">= 15") {
    val filters: Array[Filter] = Array(GreaterThanOrEqual("test", 15))
    assertScanner(meta, filters, None, Set(150, 160, 161, 162, 170, 171, 180))
  }

  test("< 5") {
    val filters: Array[Filter] = Array(LessThan("test", 5))
    assertScanner(meta, filters, None, Set(30, 31, 32, 40, 41))
  }

  test("<= 5") {
    val filters: Array[Filter] = Array(LessThanOrEqual("test", 5))
    assertScanner(meta, filters, None, Set(30, 31, 32, 40, 41, 50))
  }

  test("< 10 & > 5") {
    val filters: Array[Filter] = Array(LessThan("test", 10), GreaterThan("test", 5))
    assertScanner(meta, filters, None, Set(80, 81, 82, 90, 91))
  }

  test("> 5 & < 10") {
    val filters: Array[Filter] = Array(GreaterThan("test", 5), LessThan("test", 10))
    assertScanner(meta, filters, None, Set(80, 81, 82, 90, 91))
  }

  test("< 10 & >= 5") {
    val filters: Array[Filter] = Array(LessThan("test", 10), GreaterThanOrEqual("test", 5))
    assertScanner(meta, filters, None, Set(50, 80, 81, 82, 90, 91))
  }

  test(">= 5 & < 10") {
    val filters: Array[Filter] = Array(GreaterThanOrEqual("test", 5), LessThan("test", 10))
    assertScanner(meta, filters, None, Set(50, 80, 81, 82, 90, 91))
  }

  test("<= 10 & >= 5") {
    val filters: Array[Filter] = Array(LessThanOrEqual("test", 10), GreaterThanOrEqual("test", 5))
    assertScanner(meta, filters, None, Set(50, 80, 81, 82, 90, 91, 100))
  }

  test(">= 5 & <= 10") {
    val filters: Array[Filter] = Array(GreaterThanOrEqual("test", 5), LessThanOrEqual("test", 10))
    assertScanner(meta, filters, None, Set(50, 80, 81, 82, 90, 91, 100))
  }

  test("test fake") {
    val filters: Array[Filter] = Array(
      LessThanOrEqual("test", 10),
      GreaterThanOrEqual("test", 5),
      GreaterThan("fake", "abc"))
  }

  test(">= 10 & <= 5") {
    val filters: Array[Filter] = Array(LessThanOrEqual("test", 5), GreaterThanOrEqual("test", 10))
    assertScanner(meta, filters, None, Set())
  }

  private def assertScanner(
      meta: DataSourceMeta,
      filters: Array[Filter],
      expectedUnHandleredFilter: Option[Filter],
      expectedIds: Set[Int]): Unit = {
    val ic = new IndexContext(meta)
    val unHandledFilters = new BPlusTreeSearch(filters).unHandledFilter(ic)
    assert(unHandledFilters === expectedUnHandleredFilter)
    ic.getScannerBuilder match {
      case Some(scanner) =>
        assert(scanner.build.initialize(attemptContext).toSet === expectedIds, "")
      case None => throw new Exception(s"expect scanner, but got None")
    }
  }
}