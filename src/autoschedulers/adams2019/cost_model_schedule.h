#include "Halide.h"

using namespace Halide;

inline void do_cost_model_schedule(Halide::Pipeline pipeline) {
    // Generated by autoscheduler, manually remove unrolls.
    // Also manually replaced all RoundUp and ShiftInwards with GuardWithIf.

    using ::Halide::Func;
    using ::Halide::MemoryType;
    using ::Halide::RVar;
    using ::Halide::TailStrategy;
    using ::Halide::Var;
    Func loss_output = pipeline.get_func(55);
    Func sum_1 = pipeline.get_func(54);
    Func f2 = pipeline.get_func(53);
    Func sum = pipeline.get_func(52);
    Func prediction_output = pipeline.get_func(51);
    Func updated_bias1 = pipeline.get_func(50);
    Func bias1_im_0_d_def__ = pipeline.get_func(49);
    Func conv1_stage1_0_d_def___1 = pipeline.get_func(48);
    Func updated_filter1 = pipeline.get_func(47);
    Func filter1_im_0_d_def__ = pipeline.get_func(46);
    Func updated_head2_bias = pipeline.get_func(45);
    Func head2_bias_im_0_d_def__ = pipeline.get_func(44);
    Func head2_conv_0_d_def___1 = pipeline.get_func(43);
    Func updated_head2_filter = pipeline.get_func(42);
    Func head2_filter_im_0_d_def__ = pipeline.get_func(41);
    Func head2_conv_1_d_def__ = pipeline.get_func(40);
    Func head2_relu_0_d_def__ = pipeline.get_func(39);
    Func updated_head1_bias = pipeline.get_func(38);
    Func head1_bias_im_0_d_def__ = pipeline.get_func(37);
    Func head1_conv_0_d_def___1 = pipeline.get_func(36);
    Func updated_head1_filter = pipeline.get_func(35);
    Func head1_filter_im_0_d_def__ = pipeline.get_func(34);
    Func squashed_head1_filter_0_d_def__ = pipeline.get_func(33);
    Func squashed_head1_filter_broadcast_0_d_def__ = pipeline.get_func(32);
    Func head1_conv_1_d_def__ = pipeline.get_func(31);
    Func conv1_stage1_1_d_def__ = pipeline.get_func(30);
    Func conv1_stage2_0_d_def___1 = pipeline.get_func(29);
    Func conv1_stage2_1_d_def__ = pipeline.get_func(28);
    Func sum_1_d_def__ = pipeline.get_func(27);
    Func relu1_0_d_def__ = pipeline.get_func(26);
    Func f0_0_d_def__ = pipeline.get_func(25);
    Func f1_1_d_def__ = pipeline.get_func(24);
    Func f2_0_d_def__ = pipeline.get_func(22);
    Func sum_1_1_d_def__ = pipeline.get_func(21);
    Func loss_output_0_d_def__ = pipeline.get_func(20);
    Func adjoint = pipeline.get_func(19);
    Func f1 = pipeline.get_func(18);
    Func f0 = pipeline.get_func(17);
    Func relu1 = pipeline.get_func(16);
    Func conv1_stage2 = pipeline.get_func(15);
    Func head2_relu = pipeline.get_func(14);
    Func head2_conv = pipeline.get_func(13);
    Func normalized_schedule_features = pipeline.get_func(12);
    Func conv1_stage1 = pipeline.get_func(8);
    Func head1_conv = pipeline.get_func(7);
    Func squashed_head1_filter_broadcast = pipeline.get_func(6);
    Func squashed_head1_filter = pipeline.get_func(5);
    Var c(head2_conv_0_d_def___1.get_schedule().dims()[0].var);
    Var ci("ci");
    Var n(sum.get_schedule().dims()[0].var);
    Var ni("ni");
    Var nii("nii");
    RVar r1010_z(filter1_im_0_d_def__.update(0).get_schedule().dims()[2].var);
    RVar r1207_y(filter1_im_0_d_def__.update(1).get_schedule().dims()[1].var);
    Var s(squashed_head1_filter_0_d_def__.get_schedule().dims()[1].var);
    Var si("si");
    Var v12(head2_bias_im_0_d_def__.get_schedule().dims()[0].var);
    Var v12i("v12i");
    Var v13(head2_filter_im_0_d_def__.get_schedule().dims()[0].var);
    Var v13i("v13i");
    Var v14(head2_filter_im_0_d_def__.get_schedule().dims()[1].var);
    Var v2(bias1_im_0_d_def__.get_schedule().dims()[0].var);
    Var v207(updated_head1_filter.get_schedule().dims()[0].var);
    Var v207i("v207i");
    Var v208(updated_head1_filter.get_schedule().dims()[1].var);
    Var v208i("v208i");
    Var v209(updated_head1_filter.get_schedule().dims()[2].var);
    Var v209i("v209i");
    Var v210(updated_head1_filter.get_schedule().dims()[3].var);
    Var v210i("v210i");
    Var v211(updated_head1_bias.get_schedule().dims()[0].var);
    Var v211i("v211i");
    Var v212(updated_head1_bias.get_schedule().dims()[1].var);
    Var v213(updated_head2_filter.get_schedule().dims()[0].var);
    Var v213i("v213i");
    Var v214(updated_head2_filter.get_schedule().dims()[1].var);
    Var v214i("v214i");
    Var v215(updated_head2_filter.get_schedule().dims()[2].var);
    Var v215i("v215i");
    Var v216(updated_head2_bias.get_schedule().dims()[0].var);
    Var v216i("v216i");
    Var v217(updated_head2_bias.get_schedule().dims()[1].var);
    Var v218(updated_filter1.get_schedule().dims()[0].var);
    Var v218i("v218i");
    Var v218ii("v218ii");
    Var v219(updated_filter1.get_schedule().dims()[1].var);
    Var v219i("v219i");
    Var v220(updated_filter1.get_schedule().dims()[2].var);
    Var v220i("v220i");
    Var v221(updated_bias1.get_schedule().dims()[0].var);
    Var v221i("v221i");
    Var v222(updated_bias1.get_schedule().dims()[1].var);
    Var v2i("v2i");
    Var v3(filter1_im_0_d_def__.get_schedule().dims()[0].var);
    Var v4(filter1_im_0_d_def__.get_schedule().dims()[1].var);
    Var v4i("v4i");
    Var v5(head1_bias_im_0_d_def__.get_schedule().dims()[0].var);
    Var v5i("v5i");
    Var w(head2_conv_0_d_def___1.get_schedule().dims()[1].var);
    Var wi("wi");
    Var wii("wii");
    RVar r1010_x(filter1_im_0_d_def__.update(0).get_schedule().dims()[0].var);
    RVar r1010_y(filter1_im_0_d_def__.update(0).get_schedule().dims()[1].var);
    RVar r1029_x(conv1_stage1_1_d_def__.update(0).get_schedule().dims()[0].var);
    RVar r1029_xi("r1029$xi");
    RVar r1095_x(head2_filter_im_0_d_def__.update(0).get_schedule().dims()[0].var);
    RVar r1095_y(head2_filter_im_0_d_def__.update(0).get_schedule().dims()[1].var);
    RVar r1114_x(head2_bias_im_0_d_def__.update(0).get_schedule().dims()[0].var);
    RVar r1114_y(head2_bias_im_0_d_def__.update(0).get_schedule().dims()[1].var);
    RVar r1183_x(head1_conv_1_d_def__.update(0).get_schedule().dims()[0].var);
    RVar r1207_x(filter1_im_0_d_def__.update(1).get_schedule().dims()[0].var);
    RVar r1226_x(bias1_im_0_d_def__.update(0).get_schedule().dims()[0].var);
    RVar r1302_x(head1_bias_im_0_d_def__.update(0).get_schedule().dims()[0].var);
    RVar r1321_x(squashed_head1_filter_0_d_def__.update(0).get_schedule().dims()[0].var);
    RVar r14_x(conv1_stage1.update(0).get_schedule().dims()[0].var);
    RVar r19_x(conv1_stage2.update(0).get_schedule().dims()[0].var);
    RVar r24_x(f1.update(0).get_schedule().dims()[0].var);
    RVar r29_x(sum_1.update(0).get_schedule().dims()[0].var);
    RVar r34_x(sum.update(0).get_schedule().dims()[0].var);
    RVar r34_y(sum.update(0).get_schedule().dims()[1].var);
    RVar r4_x(head1_conv.update(0).get_schedule().dims()[0].var);
    RVar r4_y(head1_conv.update(0).get_schedule().dims()[1].var);
    RVar r9_x(head2_conv.update(0).get_schedule().dims()[0].var);
    RVar r986_x(head2_relu_0_d_def__.update(0).get_schedule().dims()[0].var);
    loss_output
        .compute_root();
    sum_1
        .compute_root();
    sum_1.update(0);
    sum
        .split(n, n, ni, 8, TailStrategy::GuardWithIf)
        .vectorize(ni)
        .compute_root()
        .reorder(ni, n)
        .parallel(n);
    sum.update(0)
        .split(n, n, ni, 8, TailStrategy::GuardWithIf)
        .vectorize(ni)
        .reorder(ni, r34_x, r34_y, n)
        .parallel(n);
    prediction_output
        .split(n, n, ni, 8, TailStrategy::GuardWithIf)
        .vectorize(ni)
        .compute_root()
        .reorder(ni, n)
        .parallel(n);
    updated_bias1
        .split(v221, v221, v221i, 8, TailStrategy::GuardWithIf)
        .vectorize(v221i)
        .compute_root()
        .reorder(v221i, v221, v222)
        .fuse(v221, v222, v221)
        .parallel(v221);
    updated_bias1.update(0)
        .split(v221, v221, v221i, 8, TailStrategy::GuardWithIf)
        .vectorize(v221i)
        .reorder(v221i, v221)
        .parallel(v221);
    updated_bias1.update(1)
        .split(v221, v221, v221i, 8, TailStrategy::GuardWithIf)
        .vectorize(v221i)
        .reorder(v221i, v221)
        .parallel(v221);
    updated_bias1.update(2)
        .split(v221, v221, v221i, 8, TailStrategy::GuardWithIf)
        .vectorize(v221i)
        .reorder(v221i, v221)
        .parallel(v221);
    updated_bias1.update(3)
        .split(v221, v221, v221i, 8, TailStrategy::GuardWithIf)
        .vectorize(v221i)
        .reorder(v221i, v221)
        .parallel(v221);
    bias1_im_0_d_def__
        .split(v2, v2, v2i, 8, TailStrategy::GuardWithIf)
        .vectorize(v2i)
        .compute_at(updated_bias1, v221)
        .reorder(v2i, v2);
    bias1_im_0_d_def__.update(0)
        .split(v2, v2, v2i, 8, TailStrategy::GuardWithIf)
        .vectorize(v2i)
        .reorder(v2i, v2, r1226_x);
    updated_filter1
        .split(v218, v218, v218i, 16, TailStrategy::GuardWithIf)
        .split(v219, v219, v219i, 2, TailStrategy::GuardWithIf)
        .split(v220, v220, v220i, 2, TailStrategy::GuardWithIf)
        .split(v218i, v218i, v218ii, 8, TailStrategy::GuardWithIf)
        .vectorize(v218ii)
        .compute_root()
        .reorder(v218ii, v218i, v219i, v220i, v218, v219, v220)
        .fuse(v219, v220, v219)
        .fuse(v218, v219, v218)
        .parallel(v218);
    updated_filter1.update(0)
        .split(v218, v218, v218i, 16, TailStrategy::GuardWithIf)
        .split(v219, v219, v219i, 2, TailStrategy::GuardWithIf)
        .split(v218i, v218i, v218ii, 8, TailStrategy::GuardWithIf)
        .vectorize(v218ii)
        .reorder(v218ii, v218i, v219i, v218, v219)
        .fuse(v218, v219, v218)
        .parallel(v218);
    updated_filter1.update(1)
        .split(v218, v218, v218i, 16, TailStrategy::GuardWithIf)
        .split(v219, v219, v219i, 2, TailStrategy::GuardWithIf)
        .split(v218i, v218i, v218ii, 8, TailStrategy::GuardWithIf)
        .vectorize(v218ii)
        .reorder(v218ii, v218i, v219i, v218, v219)
        .fuse(v218, v219, v218)
        .parallel(v218);
    updated_filter1.update(2)
        .split(v218, v218, v218i, 16, TailStrategy::GuardWithIf)
        .split(v219, v219, v219i, 2, TailStrategy::GuardWithIf)
        .split(v218i, v218i, v218ii, 8, TailStrategy::GuardWithIf)
        .vectorize(v218ii)
        .reorder(v218ii, v218i, v219i, v218, v219)
        .fuse(v218, v219, v218)
        .parallel(v218);
    updated_filter1.update(3)
        .split(v218, v218, v218i, 16, TailStrategy::GuardWithIf)
        .split(v219, v219, v219i, 2, TailStrategy::GuardWithIf)
        .split(v218i, v218i, v218ii, 8, TailStrategy::GuardWithIf)
        .vectorize(v218ii)
        .reorder(v218ii, v218i, v219i, v218, v219)
        .fuse(v218, v219, v218)
        .parallel(v218);
    filter1_im_0_d_def__
        .split(v4, v4, v4i, 8, TailStrategy::GuardWithIf)
        .vectorize(v4i)
        .compute_root()
        .reorder(v4i, v4, v3)
        .parallel(v3)
        .reorder_storage(v4, v3);
    filter1_im_0_d_def__.update(0)
        .reorder(r1010_x, r1010_y, r1010_z, v3)
        .parallel(v3);
    filter1_im_0_d_def__.update(1)
        .reorder(r1207_x, r1207_y, v3)
        .parallel(v3);
    updated_head2_bias
        .split(v216, v216, v216i, 8, TailStrategy::GuardWithIf)
        .vectorize(v216i)
        .compute_root()
        .reorder(v216i, v216, v217)
        .fuse(v216, v217, v216)
        .parallel(v216);
    updated_head2_bias.update(0)
        .split(v216, v216, v216i, 8, TailStrategy::GuardWithIf)
        .vectorize(v216i)
        .reorder(v216i, v216)
        .parallel(v216);
    updated_head2_bias.update(1)
        .split(v216, v216, v216i, 8, TailStrategy::GuardWithIf)
        .vectorize(v216i)
        .reorder(v216i, v216)
        .parallel(v216);
    updated_head2_bias.update(2)
        .split(v216, v216, v216i, 8, TailStrategy::GuardWithIf)
        .vectorize(v216i)
        .reorder(v216i, v216)
        .parallel(v216);
    updated_head2_bias.update(3)
        .split(v216, v216, v216i, 8, TailStrategy::GuardWithIf)
        .vectorize(v216i)
        .reorder(v216i, v216)
        .parallel(v216);
    head2_bias_im_0_d_def__
        .split(v12, v12, v12i, 8, TailStrategy::GuardWithIf)
        .vectorize(v12i)
        .compute_at(updated_head2_bias, v216)
        .reorder(v12i, v12);
    head2_bias_im_0_d_def__.update(0)
        .split(v12, v12, v12i, 8, TailStrategy::GuardWithIf)
        .vectorize(v12i)
        .reorder(v12i, v12, r1114_x, r1114_y);
    head2_conv_0_d_def___1
        .store_in(MemoryType::Stack)
        .split(c, c, ci, 8, TailStrategy::GuardWithIf)
        .vectorize(ci)
        .compute_at(head2_bias_im_0_d_def__, v12)
        .reorder(ci, c, w, n);
    updated_head2_filter
        .split(v213, v213, v213i, 8, TailStrategy::GuardWithIf)
        .split(v214, v214, v214i, 2, TailStrategy::GuardWithIf)
        .split(v215, v215, v215i, 2, TailStrategy::GuardWithIf)
        .vectorize(v213i)
        .compute_root()
        .reorder(v213i, v214i, v215i, v213, v214, v215)
        .fuse(v214, v215, v214)
        .fuse(v213, v214, v213)
        .parallel(v213);
    updated_head2_filter.update(0)
        .split(v213, v213, v213i, 8, TailStrategy::GuardWithIf)
        .split(v214, v214, v214i, 2, TailStrategy::GuardWithIf)
        .vectorize(v213i)
        .reorder(v213i, v214i, v213, v214)
        .fuse(v213, v214, v213)
        .parallel(v213);
    updated_head2_filter.update(1)
        .split(v213, v213, v213i, 8, TailStrategy::GuardWithIf)
        .split(v214, v214, v214i, 2, TailStrategy::GuardWithIf)
        .vectorize(v213i)
        .reorder(v213i, v214i, v213, v214)
        .fuse(v213, v214, v213)
        .parallel(v213);
    updated_head2_filter.update(2)
        .split(v213, v213, v213i, 8, TailStrategy::GuardWithIf)
        .split(v214, v214, v214i, 2, TailStrategy::GuardWithIf)
        .vectorize(v213i)
        .reorder(v213i, v214i, v213, v214)
        .fuse(v213, v214, v213)
        .parallel(v213);
    updated_head2_filter.update(3)
        .split(v213, v213, v213i, 8, TailStrategy::GuardWithIf)
        .split(v214, v214, v214i, 2, TailStrategy::GuardWithIf)
        .vectorize(v213i)
        .reorder(v213i, v214i, v213, v214)
        .fuse(v213, v214, v213)
        .parallel(v213);
    head2_filter_im_0_d_def__
        .store_in(MemoryType::Stack)
        .split(v13, v13, v13i, 8, TailStrategy::GuardWithIf)
        .vectorize(v13i)
        .compute_at(updated_head2_filter, v214i)
        .reorder(v13i, v13, v14);
    head2_filter_im_0_d_def__.update(0)
        .split(v13, v13, v13i, 8, TailStrategy::GuardWithIf)
        .vectorize(v13i)
        .reorder(v13i, v13, v14, r1095_x, r1095_y);
    head2_conv_1_d_def__
        .split(n, n, ni, 5, TailStrategy::GuardWithIf)
        .split(c, c, ci, 8, TailStrategy::GuardWithIf)
        .vectorize(ci)
        .compute_root()
        .reorder(ci, ni, c, w, n)
        .parallel(n);
    head2_relu_0_d_def__
        .store_in(MemoryType::Stack)
        .split(c, c, ci, 8, TailStrategy::GuardWithIf)
        .vectorize(ci)
        .compute_at(head2_conv_1_d_def__, c)
        .reorder(ci, c, w, n);
    head2_relu_0_d_def__.update(0)
        .split(c, c, ci, 8, TailStrategy::GuardWithIf)
        .vectorize(ci)
        .reorder(ci, c, w, n, r986_x);
    updated_head1_bias
        .split(v211, v211, v211i, 8, TailStrategy::GuardWithIf)
        .vectorize(v211i)
        .compute_root()
        .reorder(v211i, v211, v212)
        .parallel(v212);
    updated_head1_bias.update(0)
        .split(v211, v211, v211i, 8, TailStrategy::GuardWithIf)
        .vectorize(v211i)
        .reorder(v211i, v211);
    updated_head1_bias.update(1)
        .split(v211, v211, v211i, 8, TailStrategy::GuardWithIf)
        .vectorize(v211i)
        .reorder(v211i, v211);
    updated_head1_bias.update(2)
        .split(v211, v211, v211i, 8, TailStrategy::GuardWithIf)
        .vectorize(v211i)
        .reorder(v211i, v211);
    updated_head1_bias.update(3)
        .split(v211, v211, v211i, 8, TailStrategy::GuardWithIf)
        .vectorize(v211i)
        .reorder(v211i, v211);
    head1_bias_im_0_d_def__
        .split(v5, v5, v5i, 8, TailStrategy::GuardWithIf)
        .vectorize(v5i)
        .compute_root()
        .reorder(v5i, v5);
    head1_bias_im_0_d_def__.update(0)
        .split(v5, v5, v5i, 8, TailStrategy::GuardWithIf)
        .vectorize(v5i)
        .reorder(v5i, v5, r1302_x);
    updated_head1_filter
        .split(v208, v208, v208i, 2, TailStrategy::GuardWithIf)
        .split(v209, v209, v209i, 2, TailStrategy::GuardWithIf)
        .split(v210, v210, v210i, 2, TailStrategy::GuardWithIf)
        .split(v207, v207, v207i, 8, TailStrategy::GuardWithIf)
        .vectorize(v207i)
        .compute_root()
        .reorder(v207i, v207, v208i, v209i, v210i, v208, v209, v210)
        .fuse(v209, v210, v209)
        .fuse(v208, v209, v208)
        .parallel(v208);
    updated_head1_filter.update(0)
        .split(v208, v208, v208i, 2, TailStrategy::GuardWithIf)
        .split(v209, v209, v209i, 2, TailStrategy::GuardWithIf)
        .split(v207, v207, v207i, 8, TailStrategy::GuardWithIf)
        .vectorize(v207i)
        .reorder(v207i, v207, v208i, v209i, v208, v209)
        .fuse(v208, v209, v208)
        .parallel(v208);
    updated_head1_filter.update(1)
        .split(v208, v208, v208i, 2, TailStrategy::GuardWithIf)
        .split(v209, v209, v209i, 2, TailStrategy::GuardWithIf)
        .split(v207, v207, v207i, 8, TailStrategy::GuardWithIf)
        .vectorize(v207i)
        .reorder(v207i, v207, v208i, v209i, v208, v209)
        .fuse(v208, v209, v208)
        .parallel(v208);
    updated_head1_filter.update(2)
        .split(v208, v208, v208i, 2, TailStrategy::GuardWithIf)
        .split(v209, v209, v209i, 2, TailStrategy::GuardWithIf)
        .split(v207, v207, v207i, 8, TailStrategy::GuardWithIf)
        .vectorize(v207i)
        .reorder(v207i, v207, v208i, v209i, v208, v209)
        .fuse(v208, v209, v208)
        .parallel(v208);
    updated_head1_filter.update(3)
        .split(v208, v208, v208i, 2, TailStrategy::GuardWithIf)
        .split(v209, v209, v209i, 2, TailStrategy::GuardWithIf)
        .split(v207, v207, v207i, 8, TailStrategy::GuardWithIf)
        .vectorize(v207i)
        .reorder(v207i, v207, v208i, v209i, v208, v209)
        .fuse(v208, v209, v208)
        .parallel(v208);
    squashed_head1_filter_0_d_def__
        .store_in(MemoryType::Stack)
        .split(c, c, ci, 8, TailStrategy::GuardWithIf)
        .vectorize(ci)
        .compute_at(updated_head1_filter, v207)
        .reorder(ci, c, s, n);
    squashed_head1_filter_0_d_def__.update(0)
        .split(c, c, ci, 8, TailStrategy::GuardWithIf)
        .vectorize(ci)
        .reorder(ci, c, s, n, r1321_x);
    head1_conv_1_d_def__
        .split(c, c, ci, 8, TailStrategy::GuardWithIf)
        .vectorize(ci)
        .compute_root()
        .reorder(ci, c, w)
        .parallel(w);
    head1_conv_1_d_def__.update(0)
        .split(c, c, ci, 8, TailStrategy::GuardWithIf)
        .vectorize(ci)
        .reorder(ci, c, r1183_x, w)
        .parallel(w);
    conv1_stage1_1_d_def__
        .split(w, w, wi, 8, TailStrategy::GuardWithIf)
        .vectorize(wi)
        .compute_root()
        .reorder(wi, w, c)
        .parallel(c)
        .reorder_storage(w, c);
    conv1_stage1_1_d_def__.update(0)
        .split(r1029_x, r1029_x, r1029_xi, 2, TailStrategy::GuardWithIf)
        .split(w, w, wi, 8, TailStrategy::GuardWithIf)
        .vectorize(wi)
        .reorder(wi, r1029_xi, r1029_x, w, c)
        .parallel(c);
    conv1_stage2_0_d_def___1
        .store_in(MemoryType::Stack)
        .split(w, w, wi, 8, TailStrategy::GuardWithIf)
        .vectorize(wi)
        .compute_at(conv1_stage1_1_d_def__, r1029_xi)
        .store_at(conv1_stage1_1_d_def__, r1029_x)
        .reorder(wi, w, c, n)
        .reorder_storage(w, c, n);
    conv1_stage2_1_d_def__
        .split(c, c, ci, 2, TailStrategy::GuardWithIf)
        .split(n, n, ni, 8, TailStrategy::GuardWithIf)
        .vectorize(ni)
        .compute_root()
        .reorder(ni, n, ci, w, c)
        .parallel(c)
        .reorder_storage(n, c, w);
    sum_1_d_def__
        .split(n, n, ni, 8, TailStrategy::GuardWithIf)
        .vectorize(ni)
        .compute_root()
        .reorder(ni, n)
        .parallel(n);
    relu1_0_d_def__
        .split(n, n, ni, 8, TailStrategy::GuardWithIf)
        .split(w, w, wi, 2, TailStrategy::GuardWithIf)
        .vectorize(ni)
        .compute_root()
        .reorder(ni, c, wi, n, w)
        .fuse(n, w, n)
        .parallel(n)
        .reorder_storage(n, c, w);
    relu1_0_d_def__.update(0)
        .split(n, n, ni, 8, TailStrategy::GuardWithIf)
        .split(w, w, wi, 2, TailStrategy::GuardWithIf)
        .vectorize(ni)
        .reorder(ni, wi, n, w)
        .fuse(n, w, n)
        .parallel(n);
    relu1_0_d_def__.update(1)
        .split(n, n, ni, 8, TailStrategy::GuardWithIf)
        .split(w, w, wi, 2, TailStrategy::GuardWithIf)
        .vectorize(ni)
        .reorder(ni, wi, n, w)
        .fuse(n, w, n)
        .parallel(n);
    relu1_0_d_def__.update(2)
        .split(n, n, ni, 8, TailStrategy::GuardWithIf)
        .split(w, w, wi, 2, TailStrategy::GuardWithIf)
        .vectorize(ni)
        .reorder(ni, wi, n, w)
        .fuse(n, w, n)
        .parallel(n);
    relu1_0_d_def__.update(3)
        .split(n, n, ni, 8, TailStrategy::GuardWithIf)
        .split(w, w, wi, 2, TailStrategy::GuardWithIf)
        .vectorize(ni)
        .reorder(ni, wi, n, w)
        .fuse(n, w, n)
        .parallel(n);
    relu1_0_d_def__.update(4)
        .split(n, n, ni, 8, TailStrategy::GuardWithIf)
        .split(w, w, wi, 2, TailStrategy::GuardWithIf)
        .vectorize(ni)
        .reorder(ni, wi, n, w)
        .fuse(n, w, n)
        .parallel(n);
    relu1_0_d_def__.update(5)
        .split(n, n, ni, 8, TailStrategy::GuardWithIf)
        .split(w, w, wi, 2, TailStrategy::GuardWithIf)
        .vectorize(ni)
        .reorder(ni, wi, n, w)
        .fuse(n, w, n)
        .parallel(n);
    relu1_0_d_def__.update(6)
        .split(n, n, ni, 8, TailStrategy::GuardWithIf)
        .split(w, w, wi, 2, TailStrategy::GuardWithIf)
        .vectorize(ni)
        .reorder(ni, wi, n, w)
        .fuse(n, w, n)
        .parallel(n);
    relu1_0_d_def__.update(7)
        .split(n, n, ni, 8, TailStrategy::GuardWithIf)
        .split(w, w, wi, 2, TailStrategy::GuardWithIf)
        .vectorize(ni)
        .reorder(ni, wi, n, w)
        .fuse(n, w, n)
        .parallel(n);
    relu1_0_d_def__.update(8)
        .split(n, n, ni, 8, TailStrategy::GuardWithIf)
        .split(w, w, wi, 2, TailStrategy::GuardWithIf)
        .vectorize(ni)
        .reorder(ni, wi, n, w)
        .fuse(n, w, n)
        .parallel(n);
    relu1_0_d_def__.update(9)
        .split(n, n, ni, 8, TailStrategy::GuardWithIf)
        .split(w, w, wi, 2, TailStrategy::GuardWithIf)
        .vectorize(ni)
        .reorder(ni, wi, n, w)
        .fuse(n, w, n)
        .parallel(n);
    relu1_0_d_def__.update(10)
        .split(n, n, ni, 8, TailStrategy::GuardWithIf)
        .split(w, w, wi, 2, TailStrategy::GuardWithIf)
        .vectorize(ni)
        .reorder(ni, wi, n, w)
        .fuse(n, w, n)
        .parallel(n);
    relu1_0_d_def__.update(11)
        .split(n, n, ni, 8, TailStrategy::GuardWithIf)
        .split(w, w, wi, 2, TailStrategy::GuardWithIf)
        .vectorize(ni)
        .reorder(ni, wi, n, w)
        .fuse(n, w, n)
        .parallel(n);
    relu1_0_d_def__.update(12)
        .split(n, n, ni, 8, TailStrategy::GuardWithIf)
        .split(w, w, wi, 2, TailStrategy::GuardWithIf)
        .vectorize(ni)
        .reorder(ni, wi, n, w)
        .fuse(n, w, n)
        .parallel(n);
    relu1_0_d_def__.update(13)
        .split(n, n, ni, 8, TailStrategy::GuardWithIf)
        .split(w, w, wi, 2, TailStrategy::GuardWithIf)
        .vectorize(ni)
        .reorder(ni, wi, n, w)
        .fuse(n, w, n)
        .parallel(n);
    relu1_0_d_def__.update(14)
        .split(n, n, ni, 8, TailStrategy::GuardWithIf)
        .split(w, w, wi, 2, TailStrategy::GuardWithIf)
        .vectorize(ni)
        .reorder(ni, wi, n, w)
        .fuse(n, w, n)
        .parallel(n);
    relu1_0_d_def__.update(15)
        .split(n, n, ni, 8, TailStrategy::GuardWithIf)
        .split(w, w, wi, 2, TailStrategy::GuardWithIf)
        .vectorize(ni)
        .reorder(ni, wi, n, w)
        .fuse(n, w, n)
        .parallel(n);
    relu1_0_d_def__.update(16)
        .split(n, n, ni, 8, TailStrategy::GuardWithIf)
        .split(w, w, wi, 2, TailStrategy::GuardWithIf)
        .vectorize(ni)
        .reorder(ni, wi, n, w)
        .fuse(n, w, n)
        .parallel(n);
    relu1_0_d_def__.update(17)
        .split(n, n, ni, 8, TailStrategy::GuardWithIf)
        .split(w, w, wi, 2, TailStrategy::GuardWithIf)
        .vectorize(ni)
        .reorder(ni, wi, n, w)
        .fuse(n, w, n)
        .parallel(n);
    relu1_0_d_def__.update(18)
        .split(n, n, ni, 8, TailStrategy::GuardWithIf)
        .split(w, w, wi, 2, TailStrategy::GuardWithIf)
        .vectorize(ni)
        .reorder(ni, wi, n, w)
        .fuse(n, w, n)
        .parallel(n);
    relu1_0_d_def__.update(19)
        .split(n, n, ni, 8, TailStrategy::GuardWithIf)
        .split(w, w, wi, 2, TailStrategy::GuardWithIf)
        .vectorize(ni)
        .reorder(ni, wi, n, w)
        .fuse(n, w, n)
        .parallel(n);
    relu1_0_d_def__.update(20)
        .split(n, n, ni, 8, TailStrategy::GuardWithIf)
        .split(w, w, wi, 2, TailStrategy::GuardWithIf)
        .vectorize(ni)
        .reorder(ni, wi, n, w)
        .fuse(n, w, n)
        .parallel(n);
    relu1_0_d_def__.update(21)
        .split(n, n, ni, 8, TailStrategy::GuardWithIf)
        .split(w, w, wi, 2, TailStrategy::GuardWithIf)
        .vectorize(ni)
        .reorder(ni, wi, n, w)
        .fuse(n, w, n)
        .parallel(n);
    relu1_0_d_def__.update(22)
        .split(n, n, ni, 8, TailStrategy::GuardWithIf)
        .split(w, w, wi, 2, TailStrategy::GuardWithIf)
        .vectorize(ni)
        .reorder(ni, wi, n, w)
        .fuse(n, w, n)
        .parallel(n);
    relu1_0_d_def__.update(23)
        .split(n, n, ni, 8, TailStrategy::GuardWithIf)
        .split(w, w, wi, 2, TailStrategy::GuardWithIf)
        .vectorize(ni)
        .reorder(ni, wi, n, w)
        .fuse(n, w, n)
        .parallel(n);
    relu1_0_d_def__.update(24)
        .split(n, n, ni, 8, TailStrategy::GuardWithIf)
        .split(w, w, wi, 2, TailStrategy::GuardWithIf)
        .vectorize(ni)
        .reorder(ni, wi, n, w)
        .fuse(n, w, n)
        .parallel(n);
    relu1_0_d_def__.update(25)
        .split(n, n, ni, 8, TailStrategy::GuardWithIf)
        .split(w, w, wi, 2, TailStrategy::GuardWithIf)
        .vectorize(ni)
        .reorder(ni, wi, n, w)
        .fuse(n, w, n)
        .parallel(n);
    relu1_0_d_def__.update(26)
        .split(n, n, ni, 8, TailStrategy::GuardWithIf)
        .split(w, w, wi, 2, TailStrategy::GuardWithIf)
        .vectorize(ni)
        .reorder(ni, wi, n, w)
        .fuse(n, w, n)
        .parallel(n);
    relu1_0_d_def__.update(27)
        .split(n, n, ni, 8, TailStrategy::GuardWithIf)
        .split(w, w, wi, 2, TailStrategy::GuardWithIf)
        .vectorize(ni)
        .reorder(ni, wi, n, w)
        .fuse(n, w, n)
        .parallel(n);
    relu1_0_d_def__.update(28)
        .split(n, n, ni, 8, TailStrategy::GuardWithIf)
        .split(w, w, wi, 2, TailStrategy::GuardWithIf)
        .vectorize(ni)
        .reorder(ni, wi, n, w)
        .fuse(n, w, n)
        .parallel(n);
    relu1_0_d_def__.update(29)
        .split(n, n, ni, 8, TailStrategy::GuardWithIf)
        .split(w, w, wi, 2, TailStrategy::GuardWithIf)
        .vectorize(ni)
        .reorder(ni, wi, n, w)
        .fuse(n, w, n)
        .parallel(n);
    relu1_0_d_def__.update(30)
        .split(n, n, ni, 8, TailStrategy::GuardWithIf)
        .split(w, w, wi, 2, TailStrategy::GuardWithIf)
        .vectorize(ni)
        .reorder(ni, wi, n, w)
        .fuse(n, w, n)
        .parallel(n);
    relu1_0_d_def__.update(31)
        .split(n, n, ni, 8, TailStrategy::GuardWithIf)
        .split(w, w, wi, 2, TailStrategy::GuardWithIf)
        .vectorize(ni)
        .reorder(ni, wi, n, w)
        .fuse(n, w, n)
        .parallel(n);
    f0_0_d_def__
        .split(n, n, ni, 8, TailStrategy::GuardWithIf)
        .split(w, w, wi, 2, TailStrategy::GuardWithIf)
        .vectorize(ni)
        .compute_root()
        .reorder(ni, wi, n, w)
        .fuse(n, w, n)
        .parallel(n);
    f1_1_d_def__
        .split(n, n, ni, 8, TailStrategy::GuardWithIf)
        .vectorize(ni)
        .compute_at(f0_0_d_def__, n)
        .reorder(ni, n);
    sum_1_1_d_def__
        .compute_root();
    f1
        .split(n, n, ni, 8, TailStrategy::GuardWithIf)
        .vectorize(ni)
        .compute_root()
        .reorder(ni, n)
        .parallel(n);
    f1.update(0)
        .split(n, n, ni, 8, TailStrategy::GuardWithIf)
        .vectorize(ni)
        .reorder(ni, r24_x, n)
        .parallel(n);
    conv1_stage2
        .split(c, c, ci, 8, TailStrategy::GuardWithIf)
        .split(w, w, wi, 4, TailStrategy::GuardWithIf)
        .split(wi, wi, wii, 2, TailStrategy::GuardWithIf)
        .split(n, n, ni, 8, TailStrategy::GuardWithIf)
        .vectorize(ni)
        .compute_root()
        .reorder(ni, n, ci, wii, wi, c, w)
        .fuse(c, w, c)
        .parallel(c)
        .reorder_storage(n, c, w);
    conv1_stage2.update(0)
        .split(c, c, ci, 8, TailStrategy::GuardWithIf)
        .split(w, w, wi, 2, TailStrategy::GuardWithIf)
        .split(n, n, ni, 8, TailStrategy::GuardWithIf)
        .vectorize(ni)
        .reorder(ni, r19_x, n, ci, wi, c, w)
        .fuse(c, w, c)
        .parallel(c);
    head2_relu
        .split(c, c, ci, 3, TailStrategy::GuardWithIf)
        .split(w, w, wi, 7, TailStrategy::GuardWithIf)
        .split(n, n, ni, 8, TailStrategy::GuardWithIf)
        .vectorize(ni)
        .compute_root()
        .reorder(ni, n, ci, wi, c, w)
        .fuse(c, w, c)
        .parallel(c)
        .reorder_storage(n, c, w);
    head2_conv
        .split(n, n, ni, 40, TailStrategy::GuardWithIf)
        .split(c, c, ci, 12, TailStrategy::GuardWithIf)
        .split(w, w, wi, 2, TailStrategy::GuardWithIf)
        .split(ni, ni, nii, 8, TailStrategy::GuardWithIf)
        .vectorize(nii)
        .compute_root()
        .reorder(nii, ni, ci, wi, n, c, w)
        .fuse(c, w, c)
        .fuse(n, c, n)
        .parallel(n)
        .reorder_storage(n, c, w);
    head2_conv.update(0)
        .split(n, n, ni, 40, TailStrategy::GuardWithIf)
        .split(c, c, ci, 12, TailStrategy::GuardWithIf)
        .split(w, w, wi, 2, TailStrategy::GuardWithIf)
        .split(ni, ni, nii, 8, TailStrategy::GuardWithIf)
        .vectorize(nii)
        .reorder(nii, r9_x, ni, ci, wi, n, c, w)
        .fuse(c, w, c)
        .fuse(n, c, n)
        .parallel(n);
    normalized_schedule_features
        .split(c, c, ci, 5, TailStrategy::GuardWithIf)
        .split(s, s, si, 7, TailStrategy::GuardWithIf)
        .split(n, n, ni, 8, TailStrategy::GuardWithIf)
        .vectorize(ni)
        .compute_root()
        .reorder(ni, n, ci, si, c, s)
        .fuse(c, s, c)
        .parallel(c);
    conv1_stage1
        .split(c, c, ci, 8, TailStrategy::GuardWithIf)
        .vectorize(ci)
        .compute_at(conv1_stage2, c)
        .reorder(ci, c, w);
    conv1_stage1.update(0)
        .split(c, c, ci, 8, TailStrategy::GuardWithIf)
        .vectorize(ci)
        .reorder(ci, c, w, r14_x);
    head1_conv
        .split(c, c, ci, 8, TailStrategy::GuardWithIf)
        .vectorize(ci)
        .compute_root()
        .reorder(ci, c, w)
        .parallel(w);
    head1_conv.update(0)
        .split(c, c, ci, 8, TailStrategy::GuardWithIf)
        .vectorize(ci)
        .reorder(ci, c, r4_x, r4_y, w)
        .parallel(w);
    squashed_head1_filter_broadcast
        .store_in(MemoryType::Stack)
        .split(c, c, ci, 8, TailStrategy::GuardWithIf)
        .vectorize(ci)
        .compute_at(head1_conv, c)
        .reorder(ci, c, w, s, n);
    squashed_head1_filter
        .split(s, s, si, 10, TailStrategy::GuardWithIf)
        .split(n, n, ni, 2, TailStrategy::GuardWithIf)
        .split(c, c, ci, 8, TailStrategy::GuardWithIf)
        .vectorize(ci)
        .compute_root()
        .reorder(ci, c, si, ni, s, n)
        .fuse(s, n, s)
        .parallel(s);
}
