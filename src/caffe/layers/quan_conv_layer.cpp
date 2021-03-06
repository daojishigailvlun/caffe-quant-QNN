#include <vector>

#include "caffe/layers/quan_conv_layer.hpp"

#include <iostream>
#include <algorithm>
#include <limits>

namespace caffe {

  template <typename Dtype>
void QuanConvolutionLayer<Dtype>::Weight_Quantization(Dtype& weights)
  {
    Dtype scaling_factor = 0;
    Dtype min_value = 0;
    Dtype max_value = 0;

    /******************************************/

    // smart choosing between 2s complement encoding or unsigned encoding
    if (range_low_ >= 0.0) {
      // non-negative input range with unsigned range [0, 2^N-1]
      min_value = 0.0;
      max_value = pow(2.0, bit_width_) - 1.0;
    } else if (range_high_ <= 0.0) {
      // non-positive input range with unsigned range [-2^N+1, 0]
      min_value = -pow(2.0, bit_width_) + 1.0;
      max_value = 0.0;
    } else {
      // N-bit 2s complement can represent the integer between -2^(N-1)
      // to 2^(N-1)-1
      min_value = -pow(2.0, bit_width_-1);
      max_value = pow(2.0, bit_width_-1) - 1.0;
    }

    // analyze the scaling factor based on min(max)value and range
    // scaling factor should be power of 2
    // example:  scaling_factor = 2^(round(X)); X = log2(min_value / range_low), in [0,1]
    Dtype neg_scaling_factor = (range_low_ < 0) ? log2(min_value/range_low_) :
      std::numeric_limits<Dtype>::infinity();
    Dtype pos_scaling_factor = (range_high_ > 0) ? log2(max_value/range_high_) :
      std::numeric_limits<Dtype>::infinity();
    //    LOG(INFO) << " pos_scaling_factor" << pos_scaling_factor <<  "  neg_scaling_factor" << neg_scaling_factor;
    switch (round_strategy_)
      {
      case QuanInnerProductParameter_RoundStrategy_CONSERVATIVE:
	scaling_factor = pow(2.0, floor(std::min(neg_scaling_factor, pos_scaling_factor)));
	break;
      case QuanInnerProductParameter_RoundStrategy_NEUTRAL:
	scaling_factor = pow(2.0, round(std::min(neg_scaling_factor, pos_scaling_factor)));
	break;
      case QuanInnerProductParameter_RoundStrategy_AGGRESSIVE:
	scaling_factor = pow(2.0, ceil(std::min(neg_scaling_factor, pos_scaling_factor)));
	break;
      default:
	LOG(FATAL) << "Unknown round strategy.";
      }
    /******************************************/
    //LOG(INFO) << " scaling_factor" << scaling_factor << " min_value" << min_value << "   max_value" << max_value;
    Dtype weight_rounded;
    switch (round_method_) 
      {
      case QuanInnerProductParameter_RoundMethod_ROUND:
	weight_rounded = round(weights * (Dtype)scaling_factor);
	break;
      case QuanInnerProductParameter_RoundMethod_FLOOR:
	weight_rounded = floor(weights * (Dtype)scaling_factor);
	break;
      case QuanInnerProductParameter_RoundMethod_CEIL:
	weight_rounded = ceil(weights * (Dtype)scaling_factor);
	break;
      case QuanInnerProductParameter_RoundMethod_TRUNC:
	weight_rounded = trunc(weights * (Dtype)scaling_factor);
	break;
      default:
	LOG(FATAL) << "Unknown round method.";
      }
    
    weight_rounded = floor(weights * (Dtype)scaling_factor);
    // y = clip(x, min, max) / scaling_factor; so y in [min/scaling_factor, max/scaling_factor]
    weights = std::min(std::max((Dtype)weight_rounded, (Dtype)(min_value)), (Dtype)(max_value)) /
      (Dtype)(scaling_factor);
  }


  //origin quan_conv_layer below
  template <typename Dtype>
  void QuanConvolutionLayer<Dtype>::compute_output_shape() {
    const int* kernel_shape_data = this->kernel_shape_.cpu_data();
    const int* stride_data = this->stride_.cpu_data();
    const int* pad_data = this->pad_.cpu_data();
    const int* dilation_data = this->dilation_.cpu_data();
    this->output_shape_.clear();
    for (int i = 0; i < this->num_spatial_axes_; ++i) {
      // i + 1 to skip channel axis
      const int input_dim = this->input_shape(i + 1);
      const int kernel_extent = dilation_data[i] * (kernel_shape_data[i] - 1) + 1;
      const int output_dim = (input_dim + 2 * pad_data[i] - kernel_extent)
        / stride_data[i] + 1;
      this->output_shape_.push_back(output_dim);
    }
  }

  template <typename Dtype>
  void QuanConvolutionLayer<Dtype>::get_quantization_paramter() 
  {
    bit_width_ = this->layer_param_.quan_convolution_param().bit_width();
    // CHECK_GT(bit_width_, 0) << type() << " Layer has unexpected negative bit width";

    round_method_ = this->layer_param_.quan_convolution_param().round_method();
    //    round_method_ = this->round_method_;
    round_strategy_ = this->layer_param_.quan_convolution_param().round_strategy();

    // read range
    is_runtime_ = this->layer_param_.quan_convolution_param().is_runtime();
    range_low_ = this->layer_param_.quan_convolution_param().range_low();
    range_high_ = this->layer_param_.quan_convolution_param().range_high();
    if(range_low_ == range_high_ )
      is_runtime_ = 1;

    std::cout << "ydwu=======get:" << std::endl;
    std::cout << "bit_width=" << bit_width_ << ";  round_method=" << round_method_ << ";  round_strategy=" << round_strategy_ << ";  is_runtime=" << is_runtime_ << ";  range_low=" << range_low_ << ";  range_high=" << range_high_ << std::endl;
  }

  template <typename Dtype>
  void QuanConvolutionLayer<Dtype>::Forward_cpu(const vector<Blob<Dtype>*>& bottom,
						const vector<Blob<Dtype>*>& top) {
  /***********************get range*************************/
  //  LOG(INFO) << "range_high_ =" << range_high_ << ";range_low_ =" << range_low_;
  Dtype* tmp_weight = (Dtype*) malloc((this->blobs_[0]->count())*sizeof(Dtype));
  caffe_copy(this->blobs_[0]->count(), this->blobs_[0]->cpu_data(), tmp_weight);
  Dtype* Q_weight = const_cast<Dtype*>(tmp_weight);
  ///// get range_high_ and range_low_.
  //  std::cout << "old-range_high =" << range_high_ << ";   old-range_low =" << range_low_ << std::endl;
  if(is_runtime_)
    {
      Dtype* sort_weight = tmp_weight;
      int qcount_ = this->blobs_[0]->count();
      std::sort(sort_weight, sort_weight+(this->blobs_[0]->count()));
      range_high_ = sort_weight[qcount_-1];
      range_low_ = sort_weight[0];
    }

    // std::cout << "ydwu=======get:" << std::endl;
    // std::cout << "bit_width=" << bit_width_ << ";  round_method=" << round_method_ << ";  round_strategy=" << round_strategy_ << ";  is_runtime=" << is_runtime_ << ";  range_low=" << range_low_ << ";  range_high=" << range_high_ << std::endl;

    for (int i = 0; i < (this->blobs_[0]->count()); ++i) 
      {
	Weight_Quantization(*(Q_weight+i));
      }
    const Dtype *weight = Q_weight;


    /**************************************/
    // const Dtype* weight = this->blobs_[0]->cpu_data();

    //print weight to scence
    // for (int i = 0; i < 1; ++i) 
    //   std::cout << "comput--weight" << weight[i] << std::endl;

    for (int i = 0; i < bottom.size(); ++i) {
      const Dtype* bottom_data = bottom[i]->cpu_data();
      Dtype* top_data = top[i]->mutable_cpu_data();
      for (int n = 0; n < this->num_; ++n) {
	this->forward_cpu_gemm(bottom_data + n * this->bottom_dim_, weight,
			       top_data + n * this->top_dim_);
	if (this->bias_term_) {
	  const Dtype* bias = this->blobs_[1]->cpu_data();
	  this->forward_cpu_bias(top_data + n * this->top_dim_, bias);
	}
      }
    }
  free(tmp_weight);
  }

  template <typename Dtype>
  void QuanConvolutionLayer<Dtype>::Backward_cpu(const vector<Blob<Dtype>*>& top,
		const vector<bool>& propagate_down, const vector<Blob<Dtype>*>& bottom) {
    const Dtype* weight = this->blobs_[0]->cpu_data();

    // for (int i = 0; i < 1; ++i) 
    //   std::cout << "BP--weight" << weight[i] << std::endl;

    Dtype* weight_diff = this->blobs_[0]->mutable_cpu_diff();
    for (int i = 0; i < top.size(); ++i) {
      const Dtype* top_diff = top[i]->cpu_diff();
      const Dtype* bottom_data = bottom[i]->cpu_data();
      Dtype* bottom_diff = bottom[i]->mutable_cpu_diff();
      // Bias gradient, if necessary.
      if (this->bias_term_ && this->param_propagate_down_[1]) {
	Dtype* bias_diff = this->blobs_[1]->mutable_cpu_diff();
	for (int n = 0; n < this->num_; ++n) {
	  this->backward_cpu_bias(bias_diff, top_diff + n * this->top_dim_);
	}
      }
      if (this->param_propagate_down_[0] || propagate_down[i]) {
	for (int n = 0; n < this->num_; ++n) {
	  // gradient w.r.t. weight. Note that we will accumulate diffs.
	  if (this->param_propagate_down_[0]) {
	    this->weight_cpu_gemm(bottom_data + n * this->bottom_dim_,
				  top_diff + n * this->top_dim_, weight_diff);
	  }
	  // gradient w.r.t. bottom data, if necessary.
	  if (propagate_down[i]) {
	    this->backward_cpu_gemm(top_diff + n * this->top_dim_, weight,
				    bottom_diff + n * this->bottom_dim_);
	  }
	}
      }
    }
  }

#ifdef CPU_ONLY
  STUB_GPU(QuanConvolutionLayer);
#endif

  INSTANTIATE_CLASS(QuanConvolutionLayer);

}  // namespace caffe
