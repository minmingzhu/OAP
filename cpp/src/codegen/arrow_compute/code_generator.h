#pragma once

#include <arrow/pretty_print.h>
#include <arrow/record_batch.h>
#include <arrow/type.h>
#include <chrono>
#include "codegen/arrow_compute/expr_visitor.h"
#include "codegen/code_generator.h"
#include "codegen/common/result_iterator.h"
#include "utils/macros.h"

namespace sparkcolumnarplugin {
namespace codegen {
namespace arrowcompute {

class ArrowComputeCodeGenerator : public CodeGenerator {
 public:
  ArrowComputeCodeGenerator(
      std::shared_ptr<arrow::Schema> schema_ptr,
      std::vector<std::shared_ptr<gandiva::Expression>> expr_vector,
      std::vector<std::shared_ptr<arrow::Field>> ret_types, bool return_when_finish,
      std::vector<std::shared_ptr<::gandiva::Expression>> finish_exprs_vector)
      : schema_(schema_ptr),
        ret_types_(ret_types),
        return_when_finish_(return_when_finish) {
    int i = 0;
    for (auto expr : expr_vector) {
      std::shared_ptr<ExprVisitor> root_visitor;
      if (finish_exprs_vector.empty()) {
        auto visitor =
            MakeExprVisitor(schema_ptr, expr, &expr_visitor_cache_, &root_visitor);
        auto status = DistinctInsert(root_visitor, &visitor_list_);
      } else {
        auto visitor = MakeExprVisitor(schema_ptr, expr, finish_exprs_vector[i++],
                                       &expr_visitor_cache_, &root_visitor);
        auto status = DistinctInsert(root_visitor, &visitor_list_);
      }
    }
    for (auto visitor : visitor_list_) {
      auto status = visitor->Init();
    }
#ifdef DEBUG_DATA
    std::cout << "new ExprVisitor for " << schema_->ToString() << std::endl;
#endif
  }

  ~ArrowComputeCodeGenerator() {
    expr_visitor_cache_.clear();
    visitor_list_.clear();
  }

  arrow::Status DistinctInsert(const std::shared_ptr<ExprVisitor>& in,
                               std::vector<std::shared_ptr<ExprVisitor>>* visitor_list) {
    for (auto visitor : *visitor_list) {
      if (visitor == in) return arrow::Status::OK();
    }
    visitor_list->push_back(in);
    return arrow::Status::OK();
  }

  arrow::Status getSchema(std::shared_ptr<arrow::Schema>* out) {
    *out = schema_;
    return arrow::Status::OK();
  }

  arrow::Status getResSchema(std::shared_ptr<arrow::Schema>* out) {
    *out = res_schema_;
    return arrow::Status::OK();
  }

  arrow::Status SetMember(const std::shared_ptr<arrow::RecordBatch>& member_set) {
    for (auto visitor : visitor_list_) {
      RETURN_NOT_OK(visitor->SetMember(member_set));
    }
    return arrow::Status::OK();
  }

  arrow::Status SetDependency(
      const std::shared_ptr<ResultIterator<arrow::RecordBatch>>& dependency_iter,
      int index) override {
    for (auto visitor : visitor_list_) {
      RETURN_NOT_OK(visitor->SetDependency(dependency_iter, index));
    }
    return arrow::Status::OK();
  }

  arrow::Status SetResSchema(const std::shared_ptr<arrow::Schema>& in) override {
    ret_types_ = in->fields();
    return arrow::Status::OK();
  }

  arrow::Status evaluate(const std::shared_ptr<arrow::RecordBatch>& in,
                         std::vector<std::shared_ptr<arrow::RecordBatch>>* out) {
    arrow::Status status = arrow::Status::OK();
    std::vector<ArrayList> batch_array;
    std::vector<int> batch_size_array;
    std::vector<std::shared_ptr<arrow::Field>> fields;

    for (auto visitor : visitor_list_) {
      TIME_MICRO_OR_RAISE(eval_elapse_time_, visitor->Eval(in));
      if (!return_when_finish_) {
        RETURN_NOT_OK(GetResult(visitor, &batch_array, &batch_size_array, &fields));
      }
    }

    if (!return_when_finish_) {
      res_schema_ = arrow::schema(ret_types_);
      for (int i = 0; i < batch_array.size(); i++) {
        auto record_batch =
            arrow::RecordBatch::Make(res_schema_, batch_size_array[i], batch_array[i]);
#ifdef DEBUG_LEVEL_1
        std::cout << "ArrowCompute Evaluate func get output recordBatch as " << std::endl;
        auto status = arrow::PrettyPrint(*record_batch.get(), 2, &std::cout);
#endif
        out->push_back(record_batch);
        // arrow::PrettyPrint(*record_batch.get(), 2, &std::cout);
      }

      // we need to clean up this visitor chain result for next record_batch.
      for (auto visitor : visitor_list_) {
        RETURN_NOT_OK(visitor->Reset());
      }
    } else {
      for (auto visitor : visitor_list_) {
        RETURN_NOT_OK(visitor->ResetDependency());
      }
    }
    return status;
  }

  arrow::Status finish(std::vector<std::shared_ptr<arrow::RecordBatch>>* out) {
    arrow::Status status = arrow::Status::OK();
    std::vector<ArrayList> batch_array;
    std::vector<int> batch_size_array;
    std::vector<std::shared_ptr<arrow::Field>> fields;

    for (auto visitor : visitor_list_) {
      std::shared_ptr<ExprVisitor> finish_visitor;
      TIME_MICRO_OR_RAISE(finish_elapse_time_, visitor->Finish(&finish_visitor));
      if (finish_visitor) {
        RETURN_NOT_OK(
            GetResult(finish_visitor, &batch_array, &batch_size_array, &fields));
      } else {
        RETURN_NOT_OK(GetResult(visitor, &batch_array, &batch_size_array, &fields));
      }
      visitor->PrintMetrics();
      std::cout << std::endl;
    }

    res_schema_ = arrow::schema(ret_types_);
    for (int i = 0; i < batch_array.size(); i++) {
      auto record_batch =
          arrow::RecordBatch::Make(res_schema_, batch_size_array[i], batch_array[i]);
#ifdef DEBUG_LEVEL_1
      std::cout << "ArrowCompute Finish func get output recordBatch as " << std::endl;
      auto status = arrow::PrettyPrint(*record_batch.get(), 2, &std::cout);
#endif
      out->push_back(record_batch);
    }

    // we need to clean up this visitor chain result for next record_batch.
    for (auto visitor : visitor_list_) {
      RETURN_NOT_OK(visitor->Reset());
    }

    std::cout << "Evaluate took " << TIME_TO_STRING(eval_elapse_time_) << ", Finish took "
              << TIME_TO_STRING(finish_elapse_time_) << std::endl;
    return status;
  }

  arrow::Status finish(
      std::shared_ptr<ResultIterator<arrow::RecordBatch>>* out) override {
    for (auto visitor : visitor_list_) {
      TIME_MICRO_OR_RAISE(finish_elapse_time_,
                          visitor->MakeResultIterator(arrow::schema(ret_types_), out));
      visitor->PrintMetrics();
      std::cout << std::endl;
    }
    return arrow::Status::OK();
  }

 private:
  std::vector<std::shared_ptr<ExprVisitor>> visitor_list_;
  std::shared_ptr<arrow::Schema> schema_;
  std::shared_ptr<arrow::Schema> res_schema_;
  std::vector<std::shared_ptr<arrow::Field>> ret_types_;
  // metrics
  uint64_t eval_elapse_time_ = 0;
  uint64_t finish_elapse_time_ = 0;
  bool return_when_finish_;
  // ExprVisitor Cache, used when multiple node depends on same node.
  ExprVisitorMap expr_visitor_cache_;

  arrow::Status MakeBatchFromArray(std::shared_ptr<arrow::Array> column, int batch_index,
                                   std::vector<ArrayList>* batch_array,
                                   std::vector<int>* batch_size_array) {
    int res_len = 0;
    RETURN_NOT_OK(GetOrInsert(batch_index, batch_size_array, &res_len));
    batch_size_array->at(batch_index) =
        (res_len < column->length()) ? column->length() : res_len;
    ArrayList batch_array_item;
    RETURN_NOT_OK(GetOrInsert(batch_index, batch_array, &batch_array_item));
    batch_array->at(batch_index).push_back(column);
    return arrow::Status::OK();
  }
  arrow::Status MakeBatchFromArrayList(ArrayList column_list,
                                       std::vector<ArrayList>* batch_array,
                                       std::vector<int>* batch_size_array) {
    for (int i = 0; i < column_list.size(); i++) {
      RETURN_NOT_OK(MakeBatchFromArray(column_list[i], i, batch_array, batch_size_array));
    }
    return arrow::Status::OK();
  }
  arrow::Status MakeBatchFromBatch(ArrayList batch, std::vector<ArrayList>* batch_array,
                                   std::vector<int>* batch_size_array) {
    int length = 0;
    int i = 0;
    for (auto column : batch) {
      if (length != 0 && length != column->length()) {
        return arrow::Status::Invalid(
            "ArrowCompute MakeBatchFromBatch found batch contains columns with different "
            "lengths, expect ",
            length, " while got ", column->length(), " from ", i, "th column.");
      }
      length = column->length();
      i++;
    }
    batch_array->push_back(batch);
    batch_size_array->push_back(length);
    return arrow::Status::OK();
  }

  template <typename T>
  arrow::Status GetOrInsert(int i, std::vector<T>* input, T* out) {
    if (i > input->size()) {
      return arrow::Status::Invalid("GetOrInser index: ", i, "  is out of range.");
    }
    if (i == input->size()) {
      T new_data = *out;
      input->push_back(new_data);
    }
    *out = input->at(i);
    return arrow::Status::OK();
  }

  arrow::Status GetResult(std::shared_ptr<ExprVisitor> visitor,
                          std::vector<ArrayList>* batch_array,
                          std::vector<int>* batch_size_array,
                          std::vector<std::shared_ptr<arrow::Field>>* fields) {
    auto status = arrow::Status::OK();
    std::vector<std::shared_ptr<arrow::Field>> return_fields;
    switch (visitor->GetResultType()) {
      case ArrowComputeResultType::BatchList: {
        RETURN_NOT_OK(visitor->GetResult(batch_array, batch_size_array, &return_fields));
      } break;
      case ArrowComputeResultType::Batch: {
        ArrayList result_batch;
        RETURN_NOT_OK(visitor->GetResult(&result_batch, &return_fields));
        RETURN_NOT_OK(MakeBatchFromBatch(result_batch, batch_array, batch_size_array));
      } break;
      case ArrowComputeResultType::ArrayList: {
        ArrayList result_column_list;
        RETURN_NOT_OK(visitor->GetResult(&result_column_list, &return_fields));
        RETURN_NOT_OK(
            MakeBatchFromArrayList(result_column_list, batch_array, batch_size_array));
      } break;
      case ArrowComputeResultType::Array: {
        std::shared_ptr<arrow::Array> result_column;
        RETURN_NOT_OK(visitor->GetResult(&result_column, &return_fields));
        RETURN_NOT_OK(
            MakeBatchFromArray(result_column, 0, batch_array, batch_size_array));
      } break;
      default:
        return arrow::Status::Invalid("ArrowComputeResultType is invalid.");
    }
    fields->insert(fields->end(), return_fields.begin(), return_fields.end());
    return status;
  }
};  // namespace arrowcompute
}  // namespace arrowcompute
}  // namespace codegen
}  // namespace sparkcolumnarplugin
