#include "ftrl_op.h"

namespace caffe2 {

template <class T>
inline T sgn(const T x) {
  return (x == 0 ? 0 : (x < 0 ? -1 : 1));
}

template <typename T>
inline void ftrl_compute(
    const T w,
    const T n,
    const T z,
    const T g,
    T& nw,
    T& nn,
    T& nz,
    const FtrlParams<T>& params) {
  auto new_n = n + g * g;
  auto sigma = (sqrt(new_n) - sqrt(n)) / params.alpha;
  nn = new_n;
  nz = z + g - sigma * w;
  // update the weight
  if (std::abs(nz) > params.lambda1) {
    nw = (params.lambda1 * sgn(nz) - nz) /
        ((params.beta + sqrt(new_n)) / params.alpha + params.lambda2);
  } else {
    nw = 0.0;
  }
}

// TODO(dzhulgakov): implement SIMD-based version
template <typename Context, typename T>
void ftrl_update(
    int N,
    const T* w,
    const T* nz,
    const T* g,
    T* new_w,
    T* new_nz,
    const FtrlParams<T>& params,
    Context* context) {
  // TODO(cxj): use OMP when it is reliable
  // #pragma omp parallel for
  for (auto i = 0; i < N; ++i) {
    ftrl_compute(
        w[i],
        nz[i * 2],
        nz[i * 2 + 1],
        g[i],
        new_w[i],
        new_nz[i * 2],
        new_nz[i * 2 + 1],
        params);
  }
}

template <typename T, typename Context>
bool FtrlOp<T, Context>::RunOnDevice() {
  CHECK_EQ(Input(GRAD).size(), Input(VAR).size());
  CHECK_EQ(Input(GRAD).size() * 2, Input(N_Z).size());
  Output(OUTPUT_VAR)->ResizeLike(Input(VAR));
  Output(OUTPUT_N_Z)->ResizeLike(Input(N_Z));
  ftrl_update<Context>(
      Input(GRAD).size(),
      Input(VAR).template data<T>(),
      Input(N_Z).template data<T>(),
      Input(GRAD).template data<T>(),
      Output(OUTPUT_VAR)->template mutable_data<T>(),
      Output(OUTPUT_N_Z)->template mutable_data<T>(),
      params_,
      &context_);
  return true;
}

template <typename T>
template <typename SIndex>
void SparseFtrlOp<T>::DoRun() {
  auto* var = Output(OUTPUT_VAR);
  auto* n_z = Output(OUTPUT_N_Z);
  auto& indices = Input(INDICES);
  auto& grad = Input(GRAD);
  CHECK_EQ(&Input(VAR), var) << "In place operation is required";
  CHECK_EQ(&Input(N_Z), n_z) << "In place operation is required";
  TIndex M = var->size();
  TIndex N = var->dim(0);
  TIndex block_size = M / N;
  TIndex K = indices.size();
  DCHECK_EQ(M * 2, n_z->size());
  DCHECK_EQ(grad.size(), K * block_size);

  T* w = var->template mutable_data<T>();
  T* nz = n_z->template mutable_data<T>();
  const SIndex* idxs = indices.template data<SIndex>();
  const T* g = grad.template data<T>();

  // TODO(cxj): use OMP when it is reliable
  // #pragma omp parallel for
  for (TIndex i = 0; i < K; ++i) {
    SIndex idx = idxs[i];
    DCHECK(0 <= idx && idx < N) << "Index out of bounds: " << idx
                                << ", range 0 to " << N;
    if (block_size == 1) {
      ftrl_compute(
          w[idx],
          nz[idx * 2],
          nz[idx * 2 + 1],
          g[i],
          w[idx],
          nz[idx * 2],
          nz[idx * 2 + 1],
          params_);
    } else {
      TIndex x = block_size * idx;
      ftrl_update(
          block_size,
          w + x,
          nz + x * 2,
          g + i * block_size,
          w + x,
          nz + x * 2,
          params_,
          &context_);
    }
  }
}

namespace {
REGISTER_CPU_OPERATOR(Ftrl, FtrlOp<float, CPUContext>);
OPERATOR_SCHEMA(Ftrl).NumInputs(3).NumOutputs(2).AllowInplace({{0, 0}, {1, 1}});
SHOULD_NOT_DO_GRADIENT(Ftrl);

REGISTER_CPU_OPERATOR(SparseFtrl, SparseFtrlOp<float>);
OPERATOR_SCHEMA(SparseFtrl)
    .NumInputs(4)
    .NumOutputs(2)
    .EnforceInplace({{0, 0}, {1, 1}});
SHOULD_NOT_DO_GRADIENT(SparseFtrl);
}

}
