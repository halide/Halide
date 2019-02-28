// MACHINE-GENERATED - DO NOT EDIT

#include "Halide.h"

using namespace Halide;

inline void do_cost_model_schedule(Halide::Pipeline p) {
    Func loss_output = p.get_func(95);
    Func sum_1 = p.get_func(94);
    Func f2 = p.get_func(93);
    Func sum = p.get_func(92);
    Func prediction_output = p.get_func(91);
    Func updated_bias1 = p.get_func(90);
    Func constant_exterior_23 = p.get_func(89);
    Func repeat_edge_23 = p.get_func(88);
    Func bias1_im_0_d_def__ = p.get_func(87);
    Func conv1_stage1_0_d_def___1 = p.get_func(86);
    Func updated_filter1 = p.get_func(85);
    Func constant_exterior_22 = p.get_func(84);
    Func repeat_edge_22 = p.get_func(83);
    Func filter1_im_0_d_def__ = p.get_func(82);
    Func updated_head2_bias = p.get_func(81);
    Func constant_exterior_14 = p.get_func(80);
    Func repeat_edge_14 = p.get_func(79);
    Func head2_bias_im_0_d_def__ = p.get_func(78);
    Func head2_conv_0_d_def___1 = p.get_func(77);
    Func updated_head2_filter = p.get_func(76);
    Func constant_exterior_13 = p.get_func(75);
    Func repeat_edge_13 = p.get_func(74);
    Func head2_filter_im_0_d_def__ = p.get_func(73);
    Func constant_exterior_10 = p.get_func(72);
    Func repeat_edge_10 = p.get_func(71);
    Func head2_conv_1_d_def__ = p.get_func(70);
    Func constant_exterior_9 = p.get_func(69);
    Func repeat_edge_9 = p.get_func(68);
    Func head2_relu_0_d_def__ = p.get_func(67);
    Func updated_head1_bias = p.get_func(66);
    Func constant_exterior_21 = p.get_func(65);
    Func repeat_edge_21 = p.get_func(64);
    Func head1_bias_im_0_d_def__ = p.get_func(63);
    Func head1_conv_0_d_def___1 = p.get_func(62);
    Func updated_head1_filter = p.get_func(61);
    Func constant_exterior_19 = p.get_func(60);
    Func repeat_edge_19 = p.get_func(59);
    Func head1_filter_im_0_d_def__ = p.get_func(58);
    Func constant_exterior_18 = p.get_func(57);
    Func repeat_edge_18 = p.get_func(56);
    Func squashed_head1_filter_0_d_def__ = p.get_func(55);
    Func constant_exterior_17 = p.get_func(54);
    Func repeat_edge_17 = p.get_func(53);
    Func squashed_head1_filter_broadcast_0_d_def__ = p.get_func(52);
    Func constant_exterior_16 = p.get_func(51);
    Func repeat_edge_16 = p.get_func(50);
    Func head1_conv_1_d_def__ = p.get_func(49);
    Func constant_exterior_15 = p.get_func(48);
    Func repeat_edge_15 = p.get_func(47);
    Func conv1_stage1_1_d_def__ = p.get_func(46);
    Func conv1_stage2_0_d_def___1 = p.get_func(45);
    Func constant_exterior_8 = p.get_func(44);
    Func repeat_edge_8 = p.get_func(43);
    Func conv1_stage2_1_d_def__ = p.get_func(42);
    Func constant_exterior_7 = p.get_func(41);
    Func repeat_edge_7 = p.get_func(40);
    Func relu1_0_d_def__ = p.get_func(39);
    Func constant_exterior_6 = p.get_func(38);
    Func repeat_edge_6 = p.get_func(37);
    Func f0_0_d_def__ = p.get_func(36);
    Func constant_exterior_5 = p.get_func(35);
    Func repeat_edge_5 = p.get_func(34);
    Func f1_1_d_def__ = p.get_func(33);
    Func f1 = p.get_func(31);
    Func f0 = p.get_func(30);
    Func relu1 = p.get_func(29);
    Func conv1_stage2 = p.get_func(28);
    Func head2_relu = p.get_func(27);
    Func head2_conv = p.get_func(26);
    Func normalized_schedule_features = p.get_func(25);
    Func conv1_stage1 = p.get_func(21);
    Func head1_conv = p.get_func(20);
    Func squashed_head1_filter_broadcast = p.get_func(19);
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
    Var c(constant_exterior_10.get_schedule().dims()[0].var), ci("ci"), cii("cii"), n(f2.get_schedule().dims()[0].var), ni("ni"), r1001_y(head2_relu_0_d_def__.update(0).get_schedule().dims()[1].var), r1025_z(filter1_im_0_d_def__.update(0).get_schedule().dims()[2].var), r1110_z(head2_filter_im_0_d_def__.update(0).get_schedule().dims()[2].var), r1198_y(head1_conv_1_d_def__.update(0).get_schedule().dims()[1].var), r1222_y(filter1_im_0_d_def__.update(1).get_schedule().dims()[1].var), r1303_x(squashed_head1_filter_broadcast_0_d_def__.update(0).get_schedule().dims()[0].var), r1303_y(squashed_head1_filter_broadcast_0_d_def__.update(0).get_schedule().dims()[1].var), r189_x(conv1_stage2_1_d_def__.update(0).get_schedule().dims()[0].var), r189_y(conv1_stage2_1_d_def__.update(0).get_schedule().dims()[1].var), r217_x(f0_0_d_def__.update(0).get_schedule().dims()[0].var), r96_x(f2_0_d_def__.update(0).get_schedule().dims()[0].var), s(constant_exterior_18.get_schedule().dims()[1].var), v2947(updated_head1_filter.get_schedule().dims()[0].var), v2947i("v2947i"), v2948(updated_head1_filter.get_schedule().dims()[1].var), v2949(updated_head1_filter.get_schedule().dims()[2].var), v2950(updated_head1_filter.get_schedule().dims()[3].var), v2971(updated_head1_bias.get_schedule().dims()[0].var), v2971i("v2971i"), v2972(updated_head1_bias.get_schedule().dims()[1].var), v2983(updated_head2_filter.get_schedule().dims()[0].var), v2983i("v2983i"), v2983ii("v2983ii"), v2984(updated_head2_filter.get_schedule().dims()[1].var), v2985(updated_head2_filter.get_schedule().dims()[2].var), v3001(updated_head2_bias.get_schedule().dims()[0].var), v3001i("v3001i"), v3002(updated_head2_bias.get_schedule().dims()[1].var), v3013(updated_filter1.get_schedule().dims()[0].var), v3013i("v3013i"), v3013ii("v3013ii"), v3014(updated_filter1.get_schedule().dims()[1].var), v3015(updated_filter1.get_schedule().dims()[2].var), v3031(updated_bias1.get_schedule().dims()[0].var), v3031i("v3031i"), v3032(updated_bias1.get_schedule().dims()[1].var), v360(repeat_edge_23.get_schedule().dims()[0].var), v360i("v360i"), v364(constant_exterior_22.get_schedule().dims()[0].var), v364i("v364i"), v365(constant_exterior_22.get_schedule().dims()[1].var), v369(head1_bias_im_0_d_def__.get_schedule().dims()[0].var), v369i("v369i"), v383(constant_exterior_19.get_schedule().dims()[0].var), v383i("v383i"), v384(constant_exterior_19.get_schedule().dims()[1].var), v385(constant_exterior_19.get_schedule().dims()[2].var), v420(repeat_edge_14.get_schedule().dims()[0].var), v420i("v420i"), v424(constant_exterior_13.get_schedule().dims()[0].var), v424i("v424i"), v425(constant_exterior_13.get_schedule().dims()[1].var), w(constant_exterior_10.get_schedule().dims()[1].var), wi("wi");
    RVar r1001_x(head2_relu_0_d_def__.update(0).get_schedule().dims()[0].var), r1025_x(filter1_im_0_d_def__.update(0).get_schedule().dims()[0].var), r1025_y(filter1_im_0_d_def__.update(0).get_schedule().dims()[1].var), r1044_x(conv1_stage1_1_d_def__.update(0).get_schedule().dims()[0].var), r1110_x(head2_filter_im_0_d_def__.update(0).get_schedule().dims()[0].var), r1110_y(head2_filter_im_0_d_def__.update(0).get_schedule().dims()[1].var), r1129_x(head2_bias_im_0_d_def__.update(0).get_schedule().dims()[0].var), r1129_y(head2_bias_im_0_d_def__.update(0).get_schedule().dims()[1].var), r1198_x(head1_conv_1_d_def__.update(0).get_schedule().dims()[0].var), r1222_x(filter1_im_0_d_def__.update(1).get_schedule().dims()[0].var), r1241_x(bias1_im_0_d_def__.update(0).get_schedule().dims()[0].var), r1322_x(head1_bias_im_0_d_def__.update(0).get_schedule().dims()[0].var), r1341_x(squashed_head1_filter_0_d_def__.update(0).get_schedule().dims()[0].var), r14_x(conv1_stage1.update(0).get_schedule().dims()[0].var), r19_x(conv1_stage2.update(0).get_schedule().dims()[0].var), r24_x(f1.update(0).get_schedule().dims()[0].var), r29_x(sum_1.update(0).get_schedule().dims()[0].var), r34_x(sum.update(0).get_schedule().dims()[0].var), r34_y(sum.update(0).get_schedule().dims()[1].var), r4_x(head1_conv.update(0).get_schedule().dims()[0].var), r4_y(head1_conv.update(0).get_schedule().dims()[1].var), r9_x(head2_conv.update(0).get_schedule().dims()[0].var);
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
        .split(v3031, v3031, v3031i, 8, TailStrategy::ShiftInwards)
        .vectorize(v3031i)
        .compute_root()
        .reorder(v3031i, v3031, v3032);
    updated_bias1.update(0)
        .split(v3031, v3031, v3031i, 8, TailStrategy::GuardWithIf)
        .unroll(v3031)
        .vectorize(v3031i)
        .reorder(v3031i, v3031);
    updated_bias1.update(1)
        .split(v3031, v3031, v3031i, 8, TailStrategy::GuardWithIf)
        .unroll(v3031)
        .vectorize(v3031i)
        .reorder(v3031i, v3031);
    updated_bias1.update(2)
        .split(v3031, v3031, v3031i, 8, TailStrategy::GuardWithIf)
        .unroll(v3031)
        .vectorize(v3031i)
        .reorder(v3031i, v3031);
    updated_bias1.update(3)
        .split(v3031, v3031, v3031i, 8, TailStrategy::GuardWithIf)
        .unroll(v3031)
        .vectorize(v3031i)
        .reorder(v3031i, v3031);
    repeat_edge_23
        .split(v360, v360, v360i, 8, TailStrategy::RoundUp)
        .vectorize(v360i)
        .compute_at(updated_bias1, v3031)
        .reorder(v360i, v360);
    bias1_im_0_d_def__
        .split(v360, v360, v360i, 8, TailStrategy::RoundUp)
        .vectorize(v360i)
        .compute_at(updated_bias1, v3031)
        .reorder(v360i, v360);
    bias1_im_0_d_def__.update(0)
        .split(v360, v360, v360i, 8, TailStrategy::RoundUp)
        .vectorize(v360i)
        .reorder(v360i, r1241_x, v360);
    updated_filter1
        .split(v3013, v3013, v3013i, 8, TailStrategy::ShiftInwards)
        .vectorize(v3013i)
        .compute_root()
        .reorder(v3013i, v3013, v3014, v3015);
    updated_filter1.update(0)
        .split(v3013, v3013, v3013i, 16, TailStrategy::GuardWithIf)
        .split(v3013i, v3013i, v3013ii, 8, TailStrategy::GuardWithIf)
        .vectorize(v3013ii)
        .reorder(v3013ii, v3013i, v3014, v3013);
    updated_filter1.update(1)
        .split(v3013, v3013, v3013i, 8, TailStrategy::GuardWithIf)
        .vectorize(v3013i)
        .reorder(v3013i, v3013, v3014);
    updated_filter1.update(2)
        .split(v3013, v3013, v3013i, 8, TailStrategy::GuardWithIf)
        .vectorize(v3013i)
        .reorder(v3013i, v3013, v3014);
    updated_filter1.update(3)
        .split(v3013, v3013, v3013i, 8, TailStrategy::GuardWithIf)
        .vectorize(v3013i)
        .reorder(v3013i, v3013, v3014);
    constant_exterior_22
        .store_in(MemoryType::Stack)
        .split(v364, v364, v364i, 8, TailStrategy::RoundUp)
        .vectorize(v364i)
        .compute_at(updated_filter1, v3014)
        .reorder(v364i, v364, v365);
    filter1_im_0_d_def__
        .split(v364, v364, v364i, 8, TailStrategy::RoundUp)
        .vectorize(v364i)
        .compute_at(updated_filter1, v3013)
        .reorder(v364i, v364, v365);
    filter1_im_0_d_def__.update(0)
        .split(v364, v364, v364i, 8, TailStrategy::RoundUp)
        .vectorize(v364i)
        .reorder(v364i, r1025_x, r1025_y, v364, r1025_z);
    filter1_im_0_d_def__.update(1)
        .split(v364, v364, v364i, 8, TailStrategy::RoundUp)
        .vectorize(v364i)
        .reorder(v364i, r1222_x, v364, r1222_y);
    updated_head2_bias
        .split(v3001, v3001, v3001i, 8, TailStrategy::ShiftInwards)
        .vectorize(v3001i)
        .compute_root()
        .reorder(v3001i, v3001, v3002);
    updated_head2_bias.update(0)
        .split(v3001, v3001, v3001i, 8, TailStrategy::GuardWithIf)
        .unroll(v3001)
        .vectorize(v3001i)
        .reorder(v3001i, v3001);
    updated_head2_bias.update(1)
        .split(v3001, v3001, v3001i, 8, TailStrategy::GuardWithIf)
        .unroll(v3001)
        .vectorize(v3001i)
        .reorder(v3001i, v3001);
    updated_head2_bias.update(2)
        .split(v3001, v3001, v3001i, 8, TailStrategy::GuardWithIf)
        .unroll(v3001)
        .vectorize(v3001i)
        .reorder(v3001i, v3001);
    updated_head2_bias.update(3)
        .split(v3001, v3001, v3001i, 8, TailStrategy::GuardWithIf)
        .unroll(v3001)
        .vectorize(v3001i)
        .reorder(v3001i, v3001);
    repeat_edge_14
        .split(v420, v420, v420i, 8, TailStrategy::RoundUp)
        .unroll(v420)
        .vectorize(v420i)
        .compute_root()
        .reorder(v420i, v420);
    head2_bias_im_0_d_def__
        .split(v420, v420, v420i, 8, TailStrategy::RoundUp)
        .vectorize(v420i)
        .compute_at(repeat_edge_14, v420)
        .reorder(v420i, v420);
    head2_bias_im_0_d_def__.update(0)
        .split(v420, v420, v420i, 8, TailStrategy::RoundUp)
        .vectorize(v420i)
        .reorder(v420i, r1129_x, r1129_y, v420);
    updated_head2_filter
        .split(v2983, v2983, v2983i, 8, TailStrategy::ShiftInwards)
        .vectorize(v2983i)
        .compute_root()
        .reorder(v2983i, v2983, v2984, v2985);
    updated_head2_filter.update(0)
        .split(v2983, v2983, v2983i, 16, TailStrategy::GuardWithIf)
        .split(v2983i, v2983i, v2983ii, 8, TailStrategy::GuardWithIf)
        .vectorize(v2983ii)
        .reorder(v2983ii, v2983i, v2984, v2983);
    updated_head2_filter.update(1)
        .split(v2983, v2983, v2983i, 8, TailStrategy::GuardWithIf)
        .vectorize(v2983i)
        .reorder(v2983i, v2983, v2984);
    updated_head2_filter.update(2)
        .split(v2983, v2983, v2983i, 8, TailStrategy::GuardWithIf)
        .vectorize(v2983i)
        .reorder(v2983i, v2983, v2984);
    updated_head2_filter.update(3)
        .split(v2983, v2983, v2983i, 8, TailStrategy::GuardWithIf)
        .vectorize(v2983i)
        .reorder(v2983i, v2983, v2984);
    constant_exterior_13
        .store_in(MemoryType::Stack)
        .split(v424, v424, v424i, 8, TailStrategy::RoundUp)
        .vectorize(v424i)
        .compute_at(updated_head2_filter, v2984)
        .reorder(v424i, v424, v425);
    head2_filter_im_0_d_def__
        .split(v424, v424, v424i, 8, TailStrategy::RoundUp)
        .vectorize(v424i)
        .compute_at(updated_head2_filter, v2983)
        .reorder(v424i, v424, v425);
    head2_filter_im_0_d_def__.update(0)
        .split(v424, v424, v424i, 8, TailStrategy::RoundUp)
        .vectorize(v424i)
        .reorder(v424i, r1110_x, r1110_y, v424, r1110_z);
    constant_exterior_10
        .split(w, w, wi, 7, TailStrategy::ShiftInwards)
        .split(c, c, ci, 8, TailStrategy::ShiftInwards)
        .vectorize(ci)
        .compute_root()
        .reorder(ci, c, wi, w, n);
    repeat_edge_10
        .store_in(MemoryType::Stack)
        .split(c, c, ci, 8, TailStrategy::RoundUp)
        .vectorize(ci)
        .compute_at(constant_exterior_10, wi)
        .reorder(ci, c, w, n);
    head2_conv_1_d_def__
        .store_in(MemoryType::Stack)
        .split(c, c, ci, 8, TailStrategy::RoundUp)
        .vectorize(ci)
        .compute_at(constant_exterior_10, wi)
        .store_at(constant_exterior_10, w)
        .reorder(ci, c, w, n);
    repeat_edge_9
        .store_in(MemoryType::Stack)
        .split(c, c, ci, 8, TailStrategy::RoundUp)
        .vectorize(ci)
        .compute_at(constant_exterior_10, wi)
        .reorder(ci, c, w, n);
    head2_relu_0_d_def__
        .split(c, c, ci, 8, TailStrategy::RoundUp)
        .vectorize(ci)
        .compute_at(constant_exterior_10, w)
        .reorder(ci, c, w, n);
    head2_relu_0_d_def__.update(0)
        .reorder(r1001_x, r1001_y, w, n);
    updated_head1_bias
        .split(v2971, v2971, v2971i, 8, TailStrategy::ShiftInwards)
        .vectorize(v2971i)
        .compute_root()
        .reorder(v2971i, v2971, v2972);
    updated_head1_bias.update(0)
        .split(v2971, v2971, v2971i, 8, TailStrategy::GuardWithIf)
        .vectorize(v2971i)
        .reorder(v2971i, v2971);
    updated_head1_bias.update(1)
        .split(v2971, v2971, v2971i, 8, TailStrategy::GuardWithIf)
        .vectorize(v2971i)
        .reorder(v2971i, v2971);
    updated_head1_bias.update(2)
        .split(v2971, v2971, v2971i, 8, TailStrategy::GuardWithIf)
        .vectorize(v2971i)
        .reorder(v2971i, v2971);
    updated_head1_bias.update(3)
        .split(v2971, v2971, v2971i, 8, TailStrategy::GuardWithIf)
        .vectorize(v2971i)
        .reorder(v2971i, v2971);
    head1_bias_im_0_d_def__
        .split(v369, v369, v369i, 8, TailStrategy::RoundUp)
        .vectorize(v369i)
        .compute_root()
        .reorder(v369i, v369);
    head1_bias_im_0_d_def__.update(0)
        .split(v369, v369, v369i, 8, TailStrategy::RoundUp)
        .vectorize(v369i)
        .reorder(v369i, r1322_x, v369);
    updated_head1_filter
        .split(v2947, v2947, v2947i, 8, TailStrategy::ShiftInwards)
        .vectorize(v2947i)
        .compute_root()
        .reorder(v2947i, v2947, v2948, v2949, v2950);
    updated_head1_filter.update(0)
        .split(v2947, v2947, v2947i, 8, TailStrategy::GuardWithIf)
        .unroll(v2949)
        .vectorize(v2947i)
        .reorder(v2947i, v2947, v2949, v2948);
    updated_head1_filter.update(1)
        .split(v2947, v2947, v2947i, 8, TailStrategy::GuardWithIf)
        .vectorize(v2947i)
        .reorder(v2947i, v2947, v2948, v2949);
    updated_head1_filter.update(2)
        .split(v2947, v2947, v2947i, 8, TailStrategy::GuardWithIf)
        .vectorize(v2947i)
        .reorder(v2947i, v2947, v2948, v2949);
    updated_head1_filter.update(3)
        .split(v2947, v2947, v2947i, 8, TailStrategy::GuardWithIf)
        .vectorize(v2947i)
        .reorder(v2947i, v2947, v2948, v2949);
    constant_exterior_19
        .store_in(MemoryType::Stack)
        .split(v383, v383, v383i, 8, TailStrategy::RoundUp)
        .vectorize(v383i)
        .compute_at(updated_head1_filter, v2947)
        .store_at(updated_head1_filter, v2948)
        .reorder(v383i, v383, v384, v385);
    head1_filter_im_0_d_def__
        .split(v383, v383, v383i, 8, TailStrategy::ShiftInwards)
        .vectorize(v383i)
        .compute_at(updated_head1_filter, v2948)
        .reorder(v383i, v383, v384, v385);
    constant_exterior_18
        .store_in(MemoryType::Stack)
        .split(c, c, ci, 8, TailStrategy::RoundUp)
        .vectorize(ci)
        .compute_at(head1_filter_im_0_d_def__, v383)
        .store_at(updated_head1_filter, v2948)
        .reorder(ci, c, s, n);
    squashed_head1_filter_0_d_def__
        .store_in(MemoryType::Stack)
        .split(c, c, ci, 8, TailStrategy::RoundUp)
        .vectorize(ci)
        .compute_at(head1_filter_im_0_d_def__, v383)
        .reorder(ci, c, s, n);
    squashed_head1_filter_0_d_def__.update(0)
        .split(c, c, ci, 8, TailStrategy::RoundUp)
        .vectorize(ci)
        .reorder(ci, r1341_x, c, s, n);
    squashed_head1_filter_broadcast_0_d_def__
        .split(c, c, ci, 8, TailStrategy::RoundUp)
        .vectorize(ci)
        .compute_root()
        .reorder(ci, c, w, s, n);
    squashed_head1_filter_broadcast_0_d_def__.update(0)
        .split(c, c, ci, 8, TailStrategy::GuardWithIf)
        .vectorize(ci)
        .reorder(ci, c, r1303_x, r1303_y, w);
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
        .split(w, w, wi, 8, TailStrategy::RoundUp)
        .vectorize(wi)
        .compute_root()
        .reorder(wi, w, c)
        .reorder_storage(w, c);
    repeat_edge_15
        .split(w, w, wi, 8, TailStrategy::RoundUp)
        .vectorize(wi)
        .compute_at(constant_exterior_15, w)
        .reorder(wi, w, c)
        .reorder_storage(w, c);
    conv1_stage1_1_d_def__
        .split(c, c, ci, 8, TailStrategy::RoundUp)
        .vectorize(ci)
        .compute_root()
        .reorder(ci, c, w);
    conv1_stage1_1_d_def__.update(0)
        .split(c, c, ci, 8, TailStrategy::RoundUp)
        .vectorize(ci)
        .reorder(ci, c, r1044_x, w);
    conv1_stage2_0_d_def___1
        .split(c, c, ci, 8, TailStrategy::RoundUp)
        .vectorize(ci)
        .compute_at(conv1_stage1_1_d_def__, r1044_x)
        .reorder(ci, c, w, n);
    constant_exterior_8
        .split(n, n, ni, 2, TailStrategy::ShiftInwards)
        .split(c, c, ci, 16, TailStrategy::ShiftInwards)
        .split(ci, ci, cii, 8, TailStrategy::ShiftInwards)
        .unroll(ci)
        .vectorize(cii)
        .compute_root()
        .reorder(cii, ci, c, w, ni, n);
    repeat_edge_8
        .store_in(MemoryType::Stack)
        .split(c, c, ci, 8, TailStrategy::RoundUp)
        .unroll(c)
        .vectorize(ci)
        .compute_at(constant_exterior_8, c)
        .reorder(ci, c, w, n);
    conv1_stage2_1_d_def__
        .store_in(MemoryType::Stack)
        .split(w, w, wi, 8, TailStrategy::RoundUp)
        .vectorize(wi)
        .compute_at(constant_exterior_8, ni)
        .reorder(wi, w, c, n)
        .reorder_storage(w, c, n);
    conv1_stage2_1_d_def__.update(0)
        .reorder(r189_x, r189_y, n);
    conv1_stage2_1_d_def__.update(1)
        .split(w, w, wi, 8, TailStrategy::RoundUp)
        .vectorize(wi)
        .reorder(wi, w, c, n);
    relu1_0_d_def__
        .split(w, w, wi, 8, TailStrategy::RoundUp)
        .vectorize(wi)
        .compute_at(constant_exterior_8, n)
        .reorder(wi, w, c, n)
        .reorder_storage(w, c, n);
    relu1_0_d_def__.update(0)
        .split(w, w, wi, 8, TailStrategy::RoundUp)
        .vectorize(wi)
        .reorder(wi, w, n);
    relu1_0_d_def__.update(1)
        .split(w, w, wi, 8, TailStrategy::RoundUp)
        .vectorize(wi)
        .reorder(wi, w, n);
    relu1_0_d_def__.update(2)
        .split(w, w, wi, 8, TailStrategy::RoundUp)
        .vectorize(wi)
        .reorder(wi, w, n);
    relu1_0_d_def__.update(3)
        .split(w, w, wi, 8, TailStrategy::RoundUp)
        .vectorize(wi)
        .reorder(wi, w, n);
    relu1_0_d_def__.update(4)
        .split(w, w, wi, 8, TailStrategy::RoundUp)
        .vectorize(wi)
        .reorder(wi, w, n);
    relu1_0_d_def__.update(5)
        .split(w, w, wi, 8, TailStrategy::GuardWithIf)
        .vectorize(wi)
        .reorder(wi, w, n);
    relu1_0_d_def__.update(6)
        .split(w, w, wi, 8, TailStrategy::GuardWithIf)
        .vectorize(wi)
        .reorder(wi, w, n);
    relu1_0_d_def__.update(7)
        .split(w, w, wi, 8, TailStrategy::GuardWithIf)
        .vectorize(wi)
        .reorder(wi, w, n);
    relu1_0_d_def__.update(8)
        .split(w, w, wi, 8, TailStrategy::GuardWithIf)
        .vectorize(wi)
        .reorder(wi, w, n);
    relu1_0_d_def__.update(9)
        .split(w, w, wi, 8, TailStrategy::GuardWithIf)
        .vectorize(wi)
        .reorder(wi, w, n);
    relu1_0_d_def__.update(10)
        .split(w, w, wi, 8, TailStrategy::GuardWithIf)
        .vectorize(wi)
        .reorder(wi, w, n);
    relu1_0_d_def__.update(11)
        .split(w, w, wi, 8, TailStrategy::GuardWithIf)
        .vectorize(wi)
        .reorder(wi, w, n);
    relu1_0_d_def__.update(12)
        .split(w, w, wi, 8, TailStrategy::GuardWithIf)
        .vectorize(wi)
        .reorder(wi, w, n);
    relu1_0_d_def__.update(13)
        .split(w, w, wi, 8, TailStrategy::GuardWithIf)
        .vectorize(wi)
        .reorder(wi, w, n);
    relu1_0_d_def__.update(14)
        .split(w, w, wi, 8, TailStrategy::GuardWithIf)
        .vectorize(wi)
        .reorder(wi, w, n);
    relu1_0_d_def__.update(15)
        .split(w, w, wi, 8, TailStrategy::GuardWithIf)
        .vectorize(wi)
        .reorder(wi, w, n);
    relu1_0_d_def__.update(16)
        .split(w, w, wi, 8, TailStrategy::GuardWithIf)
        .vectorize(wi)
        .reorder(wi, w, n);
    relu1_0_d_def__.update(17)
        .split(w, w, wi, 8, TailStrategy::GuardWithIf)
        .vectorize(wi)
        .reorder(wi, w, n);
    relu1_0_d_def__.update(18)
        .split(w, w, wi, 8, TailStrategy::GuardWithIf)
        .vectorize(wi)
        .reorder(wi, w, n);
    relu1_0_d_def__.update(19)
        .split(w, w, wi, 8, TailStrategy::GuardWithIf)
        .vectorize(wi)
        .reorder(wi, w, n);
    relu1_0_d_def__.update(20)
        .split(w, w, wi, 8, TailStrategy::GuardWithIf)
        .vectorize(wi)
        .reorder(wi, w, n);
    relu1_0_d_def__.update(21)
        .split(w, w, wi, 8, TailStrategy::GuardWithIf)
        .vectorize(wi)
        .reorder(wi, w, n);
    relu1_0_d_def__.update(22)
        .split(w, w, wi, 8, TailStrategy::GuardWithIf)
        .vectorize(wi)
        .reorder(wi, w, n);
    relu1_0_d_def__.update(23)
        .split(w, w, wi, 8, TailStrategy::GuardWithIf)
        .vectorize(wi)
        .reorder(wi, w, n);
    relu1_0_d_def__.update(24)
        .split(w, w, wi, 8, TailStrategy::GuardWithIf)
        .vectorize(wi)
        .reorder(wi, w, n);
    relu1_0_d_def__.update(25)
        .split(w, w, wi, 8, TailStrategy::GuardWithIf)
        .vectorize(wi)
        .reorder(wi, w, n);
    relu1_0_d_def__.update(26)
        .split(w, w, wi, 8, TailStrategy::GuardWithIf)
        .vectorize(wi)
        .reorder(wi, w, n);
    relu1_0_d_def__.update(27)
        .split(w, w, wi, 8, TailStrategy::GuardWithIf)
        .vectorize(wi)
        .reorder(wi, w, n);
    relu1_0_d_def__.update(28)
        .split(w, w, wi, 8, TailStrategy::GuardWithIf)
        .vectorize(wi)
        .reorder(wi, w, n);
    relu1_0_d_def__.update(29)
        .split(w, w, wi, 8, TailStrategy::GuardWithIf)
        .vectorize(wi)
        .reorder(wi, w, n);
    relu1_0_d_def__.update(30)
        .split(w, w, wi, 8, TailStrategy::GuardWithIf)
        .vectorize(wi)
        .reorder(wi, w, n);
    relu1_0_d_def__.update(31)
        .split(w, w, wi, 8, TailStrategy::GuardWithIf)
        .vectorize(wi)
        .reorder(wi, w, n);
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
    conv1_stage2
        .split(c, c, ci, 8, TailStrategy::ShiftInwards)
        .split(n, n, ni, 16, TailStrategy::ShiftInwards)
        .unroll(ni)
        .vectorize(ci)
        .compute_root()
        .reorder(ci, ni, c, w, n);
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
        .vectorize(ci)
        .compute_at(conv1_stage2, c)
        .reorder(ci, c, w);
    conv1_stage1.update(0)
        .split(c, c, ci, 8, TailStrategy::GuardWithIf)
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
        .reorder(ci, c, w, r4_x, r4_y);
    squashed_head1_filter
        .split(c, c, ci, 8, TailStrategy::ShiftInwards)
        .vectorize(ci)
        .compute_at(head1_conv, r4_x)
        .reorder(ci, c, s, n);
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
