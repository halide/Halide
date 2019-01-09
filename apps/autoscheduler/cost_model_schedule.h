// MACHINE-GENERATED - DO NOT EDIT

#include "Halide.h"

using namespace Halide;

inline void do_cost_model_schedule(Halide::Pipeline p) {
    Func loss_output = p.get_func(91);
    Func sum_1 = p.get_func(90);
    Func f2 = p.get_func(89);
    Func sum = p.get_func(88);
    Func prediction_output = p.get_func(87);
    Func updated_bias1 = p.get_func(86);
    Func constant_exterior_22 = p.get_func(85);
    Func repeat_edge_22 = p.get_func(84);
    Func bias1_im_0_d_def__ = p.get_func(83);
    Func conv1_stage1_0_d_def___1 = p.get_func(82);
    Func updated_filter1 = p.get_func(81);
    Func constant_exterior_21 = p.get_func(80);
    Func repeat_edge_21 = p.get_func(79);
    Func filter1_im_0_d_def__ = p.get_func(78);
    Func updated_head2_bias = p.get_func(77);
    Func constant_exterior_14 = p.get_func(76);
    Func repeat_edge_14 = p.get_func(75);
    Func head2_bias_im_0_d_def__ = p.get_func(74);
    Func head2_conv_0_d_def___1 = p.get_func(73);
    Func updated_head2_filter = p.get_func(72);
    Func constant_exterior_13 = p.get_func(71);
    Func repeat_edge_13 = p.get_func(70);
    Func head2_filter_im_0_d_def__ = p.get_func(69);
    Func constant_exterior_10 = p.get_func(68);
    Func repeat_edge_10 = p.get_func(67);
    Func head2_conv_1_d_def__ = p.get_func(66);
    Func constant_exterior_9 = p.get_func(65);
    Func repeat_edge_9 = p.get_func(64);
    Func head2_relu_0_d_def__ = p.get_func(63);
    Func updated_head1_bias = p.get_func(62);
    Func constant_exterior_20 = p.get_func(61);
    Func repeat_edge_20 = p.get_func(60);
    Func head1_bias_im_0_d_def__ = p.get_func(59);
    Func head1_conv_0_d_def___1 = p.get_func(58);
    Func updated_head1_filter = p.get_func(57);
    Func constant_exterior_18 = p.get_func(56);
    Func repeat_edge_18 = p.get_func(55);
    Func head1_filter_im_0_d_def__ = p.get_func(54);
    Func constant_exterior_17 = p.get_func(53);
    Func repeat_edge_17 = p.get_func(52);
    Func squashed_head1_filter_0_d_def__ = p.get_func(51);
    Func constant_exterior_16 = p.get_func(50);
    Func repeat_edge_16 = p.get_func(49);
    Func head1_conv_1_d_def__ = p.get_func(48);
    Func constant_exterior_15 = p.get_func(47);
    Func repeat_edge_15 = p.get_func(46);
    Func conv1_stage1_1_d_def__ = p.get_func(45);
    Func conv1_stage2_0_d_def___1 = p.get_func(44);
    Func constant_exterior_8 = p.get_func(43);
    Func repeat_edge_8 = p.get_func(42);
    Func conv1_stage2_1_d_def__ = p.get_func(41);
    Func constant_exterior_7 = p.get_func(40);
    Func repeat_edge_7 = p.get_func(39);
    Func relu1_0_d_def__ = p.get_func(38);
    Func constant_exterior_6 = p.get_func(37);
    Func repeat_edge_6 = p.get_func(36);
    Func f0_0_d_def__ = p.get_func(35);
    Func constant_exterior_5 = p.get_func(34);
    Func repeat_edge_5 = p.get_func(33);
    Func f1_1_d_def__ = p.get_func(32);
    Func f1 = p.get_func(30);
    Func f0 = p.get_func(29);
    Func relu1 = p.get_func(28);
    Func conv1_stage2 = p.get_func(27);
    Func head2_relu = p.get_func(26);
    Func head2_conv = p.get_func(25);
    Func normalized_schedule_features = p.get_func(24);
    Func conv1_stage1 = p.get_func(20);
    Func head1_conv = p.get_func(19);
    Func squashed_head1_filter = p.get_func(18);
    Func constant_exterior_4 = p.get_func(12);
    Func repeat_edge_4 = p.get_func(11);
    Func sum_1_d_def__ = p.get_func(10);
    Func constant_exterior_2 = p.get_func(9);
    Func repeat_edge_2 = p.get_func(8);
    Func f2_0_d_def__ = p.get_func(7);
    Func constant_exterior_1 = p.get_func(6);
    Func repeat_edge_1 = p.get_func(5);
    Func sum_1_1_d_def__ = p.get_func(4);
    Func constant_exterior = p.get_func(3);
    Func repeat_edge = p.get_func(2);
    Func loss_output_0_d_def__ = p.get_func(1);
    Func adjoint = p.get_func(0);
    Var c(head2_conv_0_d_def___1.get_schedule().dims()[0].var), ci("ci"), cii("cii"), n(f2.get_schedule().dims()[0].var), ni("ni"), r1001_y(head2_relu_0_d_def__.update(0).get_schedule().dims()[1].var), r1025_z(filter1_im_0_d_def__.update(0).get_schedule().dims()[2].var), r1110_z(head2_filter_im_0_d_def__.update(0).get_schedule().dims()[2].var), r1198_y(head1_conv_1_d_def__.update(0).get_schedule().dims()[1].var), r1222_y(filter1_im_0_d_def__.update(1).get_schedule().dims()[1].var), r1308_y(squashed_head1_filter_0_d_def__.update(0).get_schedule().dims()[1].var), r1308_z(squashed_head1_filter_0_d_def__.update(0).get_schedule().dims()[2].var), r189_x(conv1_stage2_1_d_def__.update(0).get_schedule().dims()[0].var), r189_y(conv1_stage2_1_d_def__.update(0).get_schedule().dims()[1].var), r217_x(f0_0_d_def__.update(0).get_schedule().dims()[0].var), r96_x(f2_0_d_def__.update(0).get_schedule().dims()[0].var), s(normalized_schedule_features.get_schedule().dims()[2].var), v2820(updated_head1_filter.get_schedule().dims()[0].var), v2820i("v2820i"), v2821(updated_head1_filter.get_schedule().dims()[1].var), v2822(updated_head1_filter.get_schedule().dims()[2].var), v2823(updated_head1_filter.get_schedule().dims()[3].var), v2844(updated_head1_bias.get_schedule().dims()[0].var), v2844i("v2844i"), v2845(updated_head1_bias.get_schedule().dims()[1].var), v2856(updated_head2_filter.get_schedule().dims()[0].var), v2856i("v2856i"), v2857(updated_head2_filter.get_schedule().dims()[1].var), v2858(updated_head2_filter.get_schedule().dims()[2].var), v2874(updated_head2_bias.get_schedule().dims()[0].var), v2874i("v2874i"), v2875(updated_head2_bias.get_schedule().dims()[1].var), v2886(updated_filter1.get_schedule().dims()[0].var), v2886i("v2886i"), v2887(updated_filter1.get_schedule().dims()[1].var), v2888(updated_filter1.get_schedule().dims()[2].var), v2904(updated_bias1.get_schedule().dims()[0].var), v2904i("v2904i"), v2905(updated_bias1.get_schedule().dims()[1].var), v320(bias1_im_0_d_def__.get_schedule().dims()[0].var), v320i("v320i"), v324(constant_exterior_21.get_schedule().dims()[0].var), v324i("v324i"), v325(constant_exterior_21.get_schedule().dims()[1].var), v325i("v325i"), v329(head1_bias_im_0_d_def__.get_schedule().dims()[0].var), v329i("v329i"), v343(constant_exterior_18.get_schedule().dims()[0].var), v343i("v343i"), v344(constant_exterior_18.get_schedule().dims()[1].var), v345(constant_exterior_18.get_schedule().dims()[2].var), v372(constant_exterior_14.get_schedule().dims()[0].var), v372i("v372i"), v376(constant_exterior_13.get_schedule().dims()[0].var), v376i("v376i"), v377(constant_exterior_13.get_schedule().dims()[1].var), v377i("v377i"), w(head2_conv_0_d_def___1.get_schedule().dims()[1].var), wi("wi");
    RVar r1001_x(head2_relu_0_d_def__.update(0).get_schedule().dims()[0].var), r1025_x(filter1_im_0_d_def__.update(0).get_schedule().dims()[0].var), r1025_y(filter1_im_0_d_def__.update(0).get_schedule().dims()[1].var), r1044_x(conv1_stage1_1_d_def__.update(0).get_schedule().dims()[0].var), r1110_x(head2_filter_im_0_d_def__.update(0).get_schedule().dims()[0].var), r1110_y(head2_filter_im_0_d_def__.update(0).get_schedule().dims()[1].var), r1129_x(head2_bias_im_0_d_def__.update(0).get_schedule().dims()[0].var), r1129_y(head2_bias_im_0_d_def__.update(0).get_schedule().dims()[1].var), r1129_yi("r1129$yi"), r1198_x(head1_conv_1_d_def__.update(0).get_schedule().dims()[0].var), r1222_x(filter1_im_0_d_def__.update(1).get_schedule().dims()[0].var), r1241_x(bias1_im_0_d_def__.update(0).get_schedule().dims()[0].var), r1308_x(squashed_head1_filter_0_d_def__.update(0).get_schedule().dims()[0].var), r1327_x(head1_bias_im_0_d_def__.update(0).get_schedule().dims()[0].var), r14_x(conv1_stage1.update(0).get_schedule().dims()[0].var), r19_x(conv1_stage2.update(0).get_schedule().dims()[0].var), r24_x(f1.update(0).get_schedule().dims()[0].var), r29_x(sum_1.update(0).get_schedule().dims()[0].var), r34_x(sum.update(0).get_schedule().dims()[0].var), r34_y(sum.update(0).get_schedule().dims()[1].var), r4_x(head1_conv.update(0).get_schedule().dims()[0].var), r4_y(head1_conv.update(0).get_schedule().dims()[1].var), r9_x(head2_conv.update(0).get_schedule().dims()[0].var);
    loss_output
        .compute_root();
    sum_1
        .compute_root();
    sum_1.update(0);
    f2
        .split(n, n, ni, 8, TailStrategy::ShiftInwards)
        .unroll(n)
        .vectorize(ni)
        .compute_root()
        .reorder(ni, n);
    sum
        .split(n, n, ni, 8, TailStrategy::RoundUp)
        .vectorize(ni)
        .compute_at(f2, n)
        .reorder(ni, n);
    sum.update(0)
        .split(n, n, ni, 8, TailStrategy::RoundUp)
        .vectorize(ni)
        .reorder(ni, n, r34_x, r34_y);
    prediction_output
        .split(n, n, ni, 8, TailStrategy::ShiftInwards)
        .vectorize(ni)
        .compute_root()
        .reorder(ni, n);
    updated_bias1
        .split(v2904, v2904, v2904i, 8, TailStrategy::ShiftInwards)
        .vectorize(v2904i)
        .compute_root()
        .reorder(v2904i, v2904, v2905);
    updated_bias1.update(0)
        .split(v2904, v2904, v2904i, 8, TailStrategy::GuardWithIf)
        .unroll(v2904)
        .vectorize(v2904i)
        .reorder(v2904i, v2904);
    updated_bias1.update(1)
        .split(v2904, v2904, v2904i, 8, TailStrategy::GuardWithIf)
        .unroll(v2904)
        .vectorize(v2904i)
        .reorder(v2904i, v2904);
    updated_bias1.update(2)
        .split(v2904, v2904, v2904i, 8, TailStrategy::GuardWithIf)
        .unroll(v2904)
        .vectorize(v2904i)
        .reorder(v2904i, v2904);
    updated_bias1.update(3)
        .split(v2904, v2904, v2904i, 8, TailStrategy::GuardWithIf)
        .unroll(v2904)
        .vectorize(v2904i)
        .reorder(v2904i, v2904);
    bias1_im_0_d_def__
        .split(v320, v320, v320i, 8, TailStrategy::RoundUp)
        .vectorize(v320i)
        .compute_at(updated_bias1, v2904)
        .reorder(v320i, v320);
    bias1_im_0_d_def__.update(0)
        .split(v320, v320, v320i, 8, TailStrategy::RoundUp)
        .vectorize(v320i)
        .reorder(v320i, r1241_x, v320);
    updated_filter1
        .split(v2886, v2886, v2886i, 8, TailStrategy::ShiftInwards)
        .vectorize(v2886i)
        .compute_root()
        .reorder(v2886i, v2886, v2887, v2888);
    updated_filter1.update(0)
        .split(v2886, v2886, v2886i, 8, TailStrategy::GuardWithIf)
        .vectorize(v2886i)
        .reorder(v2886i, v2887, v2886);
    updated_filter1.update(1)
        .split(v2886, v2886, v2886i, 8, TailStrategy::GuardWithIf)
        .vectorize(v2886i)
        .reorder(v2886i, v2886, v2887);
    updated_filter1.update(2)
        .split(v2886, v2886, v2886i, 8, TailStrategy::GuardWithIf)
        .vectorize(v2886i)
        .reorder(v2886i, v2886, v2887);
    updated_filter1.update(3)
        .split(v2886, v2886, v2886i, 8, TailStrategy::GuardWithIf)
        .vectorize(v2886i)
        .reorder(v2886i, v2886, v2887);
    constant_exterior_21
        .store_in(MemoryType::Stack)
        .split(v324, v324, v324i, 8, TailStrategy::RoundUp)
        .vectorize(v324i)
        .compute_at(updated_filter1, v2887)
        .reorder(v324i, v324, v325);
    filter1_im_0_d_def__
        .split(v325, v325, v325i, 8, TailStrategy::RoundUp)
        .vectorize(v325i)
        .compute_at(updated_filter1, v2886)
        .reorder(v325i, v325, v324)
        .reorder_storage(v325, v324);
    filter1_im_0_d_def__.update(0)
        .reorder(r1025_x, r1025_y, r1025_z, v324);
    filter1_im_0_d_def__.update(1)
        .reorder(r1222_x, r1222_y, v324);
    updated_head2_bias
        .split(v2874, v2874, v2874i, 8, TailStrategy::ShiftInwards)
        .vectorize(v2874i)
        .compute_root()
        .reorder(v2874i, v2874, v2875);
    updated_head2_bias.update(0)
        .split(v2874, v2874, v2874i, 8, TailStrategy::GuardWithIf)
        .unroll(v2874)
        .vectorize(v2874i)
        .reorder(v2874i, v2874);
    updated_head2_bias.update(1)
        .split(v2874, v2874, v2874i, 8, TailStrategy::GuardWithIf)
        .unroll(v2874)
        .vectorize(v2874i)
        .reorder(v2874i, v2874);
    updated_head2_bias.update(2)
        .split(v2874, v2874, v2874i, 8, TailStrategy::GuardWithIf)
        .unroll(v2874)
        .vectorize(v2874i)
        .reorder(v2874i, v2874);
    updated_head2_bias.update(3)
        .split(v2874, v2874, v2874i, 8, TailStrategy::GuardWithIf)
        .unroll(v2874)
        .vectorize(v2874i)
        .reorder(v2874i, v2874);
    constant_exterior_14
        .split(v372, v372, v372i, 8, TailStrategy::RoundUp)
        .vectorize(v372i)
        .compute_at(updated_head2_bias, v2874)
        .reorder(v372i, v372);
    head2_bias_im_0_d_def__
        .split(v372, v372, v372i, 8, TailStrategy::RoundUp)
        .vectorize(v372i)
        .compute_at(updated_head2_bias, v2874)
        .reorder(v372i, v372);
    head2_bias_im_0_d_def__.update(0)
        .split(r1129_y, r1129_y, r1129_yi, 2, TailStrategy::GuardWithIf)
        .split(v372, v372, v372i, 8, TailStrategy::RoundUp)
        .vectorize(v372i)
        .reorder(v372i, r1129_yi, v372, r1129_x, r1129_y);
    head2_conv_0_d_def___1
        .store_in(MemoryType::Stack)
        .split(c, c, ci, 8, TailStrategy::RoundUp)
        .vectorize(ci)
        .compute_at(head2_bias_im_0_d_def__, r1129_x)
        .reorder(ci, c, w, n);
    updated_head2_filter
        .split(v2856, v2856, v2856i, 8, TailStrategy::ShiftInwards)
        .vectorize(v2856i)
        .compute_root()
        .reorder(v2856i, v2856, v2857, v2858);
    updated_head2_filter.update(0)
        .split(v2856, v2856, v2856i, 8, TailStrategy::GuardWithIf)
        .vectorize(v2856i)
        .reorder(v2856i, v2856, v2857);
    updated_head2_filter.update(1)
        .split(v2856, v2856, v2856i, 8, TailStrategy::GuardWithIf)
        .vectorize(v2856i)
        .reorder(v2856i, v2856, v2857);
    updated_head2_filter.update(2)
        .split(v2856, v2856, v2856i, 8, TailStrategy::GuardWithIf)
        .vectorize(v2856i)
        .reorder(v2856i, v2856, v2857);
    updated_head2_filter.update(3)
        .split(v2856, v2856, v2856i, 8, TailStrategy::GuardWithIf)
        .vectorize(v2856i)
        .reorder(v2856i, v2856, v2857);
    constant_exterior_13
        .split(v376, v376, v376i, 8, TailStrategy::RoundUp)
        .vectorize(v376i)
        .compute_at(updated_head2_filter, v2856)
        .reorder(v376i, v376, v377);
    head2_filter_im_0_d_def__
        .split(v377, v377, v377i, 8, TailStrategy::RoundUp)
        .vectorize(v377i)
        .compute_root()
        .reorder(v377i, v377, v376)
        .reorder_storage(v377, v376);
    head2_filter_im_0_d_def__.update(0)
        .reorder(r1110_x, r1110_y, r1110_z, v376);
    constant_exterior_10
        .split(c, c, ci, 8, TailStrategy::ShiftInwards)
        .vectorize(ci)
        .compute_root()
        .reorder(ci, c, w, n);
    repeat_edge_10
        .store_in(MemoryType::Stack)
        .split(c, c, ci, 8, TailStrategy::RoundUp)
        .vectorize(ci)
        .compute_at(constant_exterior_10, c)
        .reorder(ci, c, w, n);
    head2_relu_0_d_def__
        .split(c, c, ci, 8, TailStrategy::RoundUp)
        .vectorize(ci)
        .compute_at(constant_exterior_10, w)
        .reorder(ci, c, w, n);
    head2_relu_0_d_def__.update(0)
        .reorder(r1001_x, r1001_y, w, n);
    updated_head1_bias
        .split(v2844, v2844, v2844i, 8, TailStrategy::ShiftInwards)
        .vectorize(v2844i)
        .compute_root()
        .reorder(v2844i, v2844, v2845);
    updated_head1_bias.update(0)
        .split(v2844, v2844, v2844i, 8, TailStrategy::GuardWithIf)
        .vectorize(v2844i)
        .reorder(v2844i, v2844);
    updated_head1_bias.update(1)
        .split(v2844, v2844, v2844i, 8, TailStrategy::GuardWithIf)
        .vectorize(v2844i)
        .reorder(v2844i, v2844);
    updated_head1_bias.update(2)
        .split(v2844, v2844, v2844i, 8, TailStrategy::GuardWithIf)
        .vectorize(v2844i)
        .reorder(v2844i, v2844);
    updated_head1_bias.update(3)
        .split(v2844, v2844, v2844i, 8, TailStrategy::GuardWithIf)
        .vectorize(v2844i)
        .reorder(v2844i, v2844);
    head1_bias_im_0_d_def__
        .split(v329, v329, v329i, 8, TailStrategy::RoundUp)
        .vectorize(v329i)
        .compute_root()
        .reorder(v329i, v329);
    head1_bias_im_0_d_def__.update(0)
        .split(v329, v329, v329i, 8, TailStrategy::RoundUp)
        .vectorize(v329i)
        .reorder(v329i, r1327_x, v329);
    updated_head1_filter
        .split(v2820, v2820, v2820i, 8, TailStrategy::ShiftInwards)
        .vectorize(v2820i)
        .compute_root()
        .reorder(v2820i, v2820, v2821, v2822, v2823);
    updated_head1_filter.update(0)
        .split(v2820, v2820, v2820i, 8, TailStrategy::GuardWithIf)
        .vectorize(v2820i)
        .reorder(v2820i, v2820, v2821, v2822);
    updated_head1_filter.update(1)
        .split(v2820, v2820, v2820i, 8, TailStrategy::GuardWithIf)
        .vectorize(v2820i)
        .reorder(v2820i, v2820, v2821, v2822);
    updated_head1_filter.update(2)
        .split(v2820, v2820, v2820i, 8, TailStrategy::GuardWithIf)
        .vectorize(v2820i)
        .reorder(v2820i, v2820, v2821, v2822);
    updated_head1_filter.update(3)
        .split(v2820, v2820, v2820i, 8, TailStrategy::GuardWithIf)
        .vectorize(v2820i)
        .reorder(v2820i, v2820, v2821, v2822);
    constant_exterior_18
        .split(v343, v343, v343i, 8, TailStrategy::RoundUp)
        .vectorize(v343i)
        .compute_at(updated_head1_filter, v2820)
        .reorder(v343i, v343, v344, v345);
    head1_filter_im_0_d_def__
        .split(v343, v343, v343i, 8, TailStrategy::ShiftInwards)
        .vectorize(v343i)
        .compute_at(updated_head1_filter, v2820)
        .reorder(v343i, v343, v344, v345);
    squashed_head1_filter_0_d_def__
        .split(w, w, wi, 8, TailStrategy::RoundUp)
        .vectorize(wi)
        .compute_root()
        .reorder(wi, w, c, n)
        .reorder_storage(w, c, n);
    squashed_head1_filter_0_d_def__.update(0)
        .reorder(r1308_x, r1308_y, r1308_z, c);
    constant_exterior_16
        .split(c, c, ci, 8, TailStrategy::ShiftInwards)
        .vectorize(ci)
        .compute_root()
        .reorder(ci, c, w);
    repeat_edge_16
        .split(c, c, ci, 8, TailStrategy::RoundUp)
        .vectorize(ci)
        .compute_at(constant_exterior_16, c)
        .reorder(ci, c, w);
    head1_conv_1_d_def__
        .split(c, c, ci, 8, TailStrategy::RoundUp)
        .vectorize(ci)
        .compute_at(constant_exterior_16, c)
        .reorder(ci, c, w);
    head1_conv_1_d_def__.update(0)
        .reorder(r1198_x, r1198_y, w);
    constant_exterior_15
        .split(c, c, ci, 8, TailStrategy::RoundUp)
        .vectorize(ci)
        .compute_root()
        .reorder(ci, c, w);
    conv1_stage1_1_d_def__
        .split(c, c, ci, 8, TailStrategy::RoundUp)
        .vectorize(ci)
        .compute_at(constant_exterior_15, c)
        .reorder(ci, c, w);
    conv1_stage1_1_d_def__.update(0)
        .split(c, c, ci, 8, TailStrategy::RoundUp)
        .vectorize(ci)
        .reorder(ci, r1044_x, c, w);
    constant_exterior_8
        .split(n, n, ni, 2, TailStrategy::ShiftInwards)
        .split(c, c, ci, 8, TailStrategy::ShiftInwards)
        .vectorize(ci)
        .compute_root()
        .reorder(ci, c, w, ni, n);
    repeat_edge_8
        .store_in(MemoryType::Stack)
        .split(c, c, ci, 8, TailStrategy::RoundUp)
        .vectorize(ci)
        .compute_at(constant_exterior_8, c)
        .reorder(ci, c, w, n);
    conv1_stage2_1_d_def__
        .store_in(MemoryType::Stack)
        .split(c, c, ci, 8, TailStrategy::RoundUp)
        .vectorize(ci)
        .compute_at(constant_exterior_8, ni)
        .reorder(ci, c, w, n);
    conv1_stage2_1_d_def__.update(0)
        .reorder(r189_x, r189_y, n);
    conv1_stage2_1_d_def__.update(1)
        .split(c, c, ci, 8, TailStrategy::RoundUp)
        .vectorize(ci)
        .reorder(ci, c, w, n);
    relu1_0_d_def__
        .split(c, c, ci, 8, TailStrategy::RoundUp)
        .vectorize(ci)
        .compute_at(constant_exterior_8, n)
        .reorder(ci, c, w, n);
    relu1_0_d_def__.update(0)
        .reorder(w, n);
    relu1_0_d_def__.update(1)
        .reorder(w, n);
    relu1_0_d_def__.update(2)
        .reorder(w, n);
    relu1_0_d_def__.update(3)
        .reorder(w, n);
    relu1_0_d_def__.update(4)
        .reorder(w, n);
    relu1_0_d_def__.update(5)
        .reorder(w, n);
    relu1_0_d_def__.update(6)
        .reorder(w, n);
    relu1_0_d_def__.update(7)
        .reorder(w, n);
    relu1_0_d_def__.update(8)
        .reorder(w, n);
    relu1_0_d_def__.update(9)
        .reorder(w, n);
    relu1_0_d_def__.update(10)
        .reorder(w, n);
    relu1_0_d_def__.update(11)
        .reorder(w, n);
    relu1_0_d_def__.update(12)
        .reorder(w, n);
    relu1_0_d_def__.update(13)
        .reorder(w, n);
    relu1_0_d_def__.update(14)
        .reorder(w, n);
    relu1_0_d_def__.update(15)
        .reorder(w, n);
    relu1_0_d_def__.update(16)
        .reorder(w, n);
    relu1_0_d_def__.update(17)
        .reorder(w, n);
    relu1_0_d_def__.update(18)
        .reorder(w, n);
    relu1_0_d_def__.update(19)
        .reorder(w, n);
    relu1_0_d_def__.update(20)
        .reorder(w, n);
    relu1_0_d_def__.update(21)
        .reorder(w, n);
    relu1_0_d_def__.update(22)
        .reorder(w, n);
    relu1_0_d_def__.update(23)
        .reorder(w, n);
    relu1_0_d_def__.update(24)
        .reorder(w, n);
    relu1_0_d_def__.update(25)
        .reorder(w, n);
    relu1_0_d_def__.update(26)
        .reorder(w, n);
    relu1_0_d_def__.update(27)
        .reorder(w, n);
    relu1_0_d_def__.update(28)
        .reorder(w, n);
    relu1_0_d_def__.update(29)
        .reorder(w, n);
    relu1_0_d_def__.update(30)
        .reorder(w, n);
    relu1_0_d_def__.update(31)
        .reorder(w, n);
    constant_exterior_6
        .split(w, w, wi, 8, TailStrategy::RoundUp)
        .vectorize(wi)
        .compute_at(constant_exterior_8, n)
        .reorder(wi, w, n)
        .reorder_storage(w, n);
    f0_0_d_def__
        .store_in(MemoryType::Stack)
        .split(w, w, wi, 8, TailStrategy::RoundUp)
        .vectorize(wi)
        .compute_at(constant_exterior_6, n)
        .reorder(wi, w, n)
        .reorder_storage(w, n);
    f0_0_d_def__.update(0)
        .reorder(r217_x, n);
    constant_exterior_5
        .split(n, n, ni, 8, TailStrategy::ShiftInwards)
        .vectorize(ni)
        .compute_root()
        .reorder(ni, n);
    f1
        .split(n, n, ni, 8, TailStrategy::RoundUp)
        .vectorize(ni)
        .compute_root()
        .reorder(ni, n);
    f1.update(0)
        .split(n, n, ni, 8, TailStrategy::GuardWithIf)
        .vectorize(ni)
        .reorder(ni, r24_x, n);
    f0
        .split(n, n, ni, 8, TailStrategy::ShiftInwards)
        .vectorize(ni)
        .compute_at(f1, r24_x)
        .reorder(ni, n, w);
    conv1_stage2
        .split(c, c, ci, 16, TailStrategy::ShiftInwards)
        .split(ci, ci, cii, 8, TailStrategy::ShiftInwards)
        .vectorize(cii)
        .compute_root()
        .reorder(cii, ci, n, c, w);
    conv1_stage2.update(0)
        .split(c, c, ci, 8, TailStrategy::GuardWithIf)
        .vectorize(ci)
        .reorder(ci, r19_x, c, w, n);
    head2_relu
        .split(n, n, ni, 8, TailStrategy::RoundUp)
        .vectorize(ni)
        .compute_root()
        .reorder(ni, n, c, w)
        .reorder_storage(n, c, w);
    head2_conv
        .split(c, c, ci, 8, TailStrategy::ShiftInwards)
        .vectorize(ci)
        .compute_root()
        .reorder(ci, c, w, n);
    head2_conv.update(0)
        .split(c, c, ci, 8, TailStrategy::GuardWithIf)
        .vectorize(ci)
        .reorder(ci, r9_x, c, w, n);
    normalized_schedule_features
        .split(n, n, ni, 8, TailStrategy::ShiftInwards)
        .vectorize(ni)
        .compute_root()
        .reorder(ni, n, c, s);
    conv1_stage1
        .split(c, c, ci, 8, TailStrategy::ShiftInwards)
        .unroll(c)
        .vectorize(ci)
        .compute_at(conv1_stage2, c)
        .reorder(ci, c, w);
    conv1_stage1.update(0)
        .split(c, c, ci, 8, TailStrategy::GuardWithIf)
        .unroll(c)
        .vectorize(ci)
        .reorder(ci, c, w, r14_x);
    head1_conv
        .split(c, c, ci, 8, TailStrategy::ShiftInwards)
        .vectorize(ci)
        .compute_root()
        .reorder(ci, c, w);
    head1_conv.update(0)
        .split(c, c, ci, 8, TailStrategy::GuardWithIf)
        .vectorize(ci)
        .reorder(ci, r4_y, c, w, r4_x);
    squashed_head1_filter
        .store_in(MemoryType::Stack)
        .split(c, c, ci, 8, TailStrategy::ShiftInwards)
        .vectorize(ci)
        .compute_at(head1_conv, r4_y)
        .store_at(head1_conv, r4_x)
        .reorder(ci, c, w, n);
    constant_exterior_4
        .split(n, n, ni, 8, TailStrategy::RoundUp)
        .vectorize(ni)
        .compute_root()
        .reorder(ni, n);
    f2_0_d_def__
        .split(n, n, ni, 8, TailStrategy::RoundUp)
        .vectorize(ni)
        .compute_root()
        .reorder(ni, n);
    f2_0_d_def__.update(0);
    constant_exterior_1
        .compute_root();
}
