// MACHINE-GENERATED - DO NOT EDIT

#include "Halide.h"

using namespace Halide;

inline void do_cost_model_schedule(Halide::Pipeline p) {
    for (int i = 0; i < 91; i++) {
        p.get_func(i).compute_root();
    }
    return;

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
    Var c(conv1_stage1_0_d_def___1.get_schedule().dims()[0].var), ci("ci"), cii("cii"), ciii("ciii"), n(sum.get_schedule().dims()[0].var), ni("ni"), nii("nii"), niii("niii"), r1068_y(head1_conv_1_d_def__.update(0).get_schedule().dims()[1].var), r1092_y(filter1_im_0_d_def__.update(1).get_schedule().dims()[1].var), r1178_y(squashed_head1_filter_0_d_def__.update(0).get_schedule().dims()[1].var), r1178_z(squashed_head1_filter_0_d_def__.update(0).get_schedule().dims()[2].var), r227_x(conv1_stage2_1_d_def__.update(0).get_schedule().dims()[0].var), r227_y(conv1_stage2_1_d_def__.update(0).get_schedule().dims()[1].var), r255_x(f0_0_d_def__.update(0).get_schedule().dims()[0].var), r871_y(head2_relu_0_d_def__.update(0).get_schedule().dims()[1].var), r871_yi("r871$yi"), r895_z(filter1_im_0_d_def__.update(0).get_schedule().dims()[2].var), r96_x(f2_0_d_def__.update(0).get_schedule().dims()[0].var), r980_z(head2_filter_im_0_d_def__.update(0).get_schedule().dims()[2].var), s(normalized_schedule_features.get_schedule().dims()[2].var), v2533(updated_head1_filter.get_schedule().dims()[0].var), v2533i("v2533i"), v2533ii("v2533ii"), v2534(updated_head1_filter.get_schedule().dims()[1].var), v2534i("v2534i"), v2535(updated_head1_filter.get_schedule().dims()[2].var), v2536(updated_head1_filter.get_schedule().dims()[3].var), v2557(updated_head1_bias.get_schedule().dims()[0].var), v2557i("v2557i"), v2558(updated_head1_bias.get_schedule().dims()[1].var), v2569(updated_head2_filter.get_schedule().dims()[0].var), v2569i("v2569i"), v2569ii("v2569ii"), v2570(updated_head2_filter.get_schedule().dims()[1].var), v2571(updated_head2_filter.get_schedule().dims()[2].var), v2587(updated_head2_bias.get_schedule().dims()[0].var), v2587i("v2587i"), v2588(updated_head2_bias.get_schedule().dims()[1].var), v2599(updated_filter1.get_schedule().dims()[0].var), v2599i("v2599i"), v2599ii("v2599ii"), v2600(updated_filter1.get_schedule().dims()[1].var), v2600i("v2600i"), v2601(updated_filter1.get_schedule().dims()[2].var), v2617(updated_bias1.get_schedule().dims()[0].var), v2617i("v2617i"), v2618(updated_bias1.get_schedule().dims()[1].var), v320(constant_exterior_22.get_schedule().dims()[0].var), v320i("v320i"), v324(constant_exterior_21.get_schedule().dims()[0].var), v324i("v324i"), v324ii("v324ii"), v325(constant_exterior_21.get_schedule().dims()[1].var), v329(repeat_edge_20.get_schedule().dims()[0].var), v329i("v329i"), v343(constant_exterior_18.get_schedule().dims()[0].var), v343i("v343i"), v343ii("v343ii"), v344(constant_exterior_18.get_schedule().dims()[1].var), v344i("v344i"), v345(constant_exterior_18.get_schedule().dims()[2].var), v372(constant_exterior_14.get_schedule().dims()[0].var), v372i("v372i"), v376(constant_exterior_13.get_schedule().dims()[0].var), v376i("v376i"), v376ii("v376ii"), v377(constant_exterior_13.get_schedule().dims()[1].var), w(conv1_stage1_0_d_def___1.get_schedule().dims()[1].var), wi("wi"), wii("wii");
    RVar r1068_x(head1_conv_1_d_def__.update(0).get_schedule().dims()[0].var), r1092_x(filter1_im_0_d_def__.update(1).get_schedule().dims()[0].var), r1111_x(bias1_im_0_d_def__.update(0).get_schedule().dims()[0].var), r1178_x(squashed_head1_filter_0_d_def__.update(0).get_schedule().dims()[0].var), r1197_x(head1_bias_im_0_d_def__.update(0).get_schedule().dims()[0].var), r124_x(f1_1_d_def__.update(0).get_schedule().dims()[0].var), r14_x(conv1_stage1.update(0).get_schedule().dims()[0].var), r19_x(conv1_stage2.update(0).get_schedule().dims()[0].var), r24_x(f1.update(0).get_schedule().dims()[0].var), r29_x(sum_1.update(0).get_schedule().dims()[0].var), r34_x(sum.update(0).get_schedule().dims()[0].var), r34_y(sum.update(0).get_schedule().dims()[1].var), r4_x(head1_conv.update(0).get_schedule().dims()[0].var), r4_y(head1_conv.update(0).get_schedule().dims()[1].var), r871_x(head2_relu_0_d_def__.update(0).get_schedule().dims()[0].var), r895_x(filter1_im_0_d_def__.update(0).get_schedule().dims()[0].var), r895_y(filter1_im_0_d_def__.update(0).get_schedule().dims()[1].var), r9_x(head2_conv.update(0).get_schedule().dims()[0].var), r914_x(conv1_stage1_1_d_def__.update(0).get_schedule().dims()[0].var), r980_x(head2_filter_im_0_d_def__.update(0).get_schedule().dims()[0].var), r980_y(head2_filter_im_0_d_def__.update(0).get_schedule().dims()[1].var), r999_x(head2_bias_im_0_d_def__.update(0).get_schedule().dims()[0].var), r999_y(head2_bias_im_0_d_def__.update(0).get_schedule().dims()[1].var);
    loss_output
        .compute_root();
    sum_1
        .compute_root();
    sum
        .split(n, n, ni, 8, TailStrategy::RoundUp)
        .unroll(n)
        .vectorize(ni)
        .compute_root()
        .reorder(ni, n)
        .serial(n);
    sum.update(0)
        .split(n, n, ni, 8, TailStrategy::RoundUp)
        .split(ni, ni, nii, 8, TailStrategy::GuardWithIf)
        .vectorize(nii)
        .reorder(nii, ni, r34_x, r34_y, n)
        .serial(n);
    prediction_output
        .split(n, n, ni, 8, TailStrategy::ShiftInwards)
        .unroll(n)
        .vectorize(ni)
        .compute_root()
        .reorder(ni, n)
        .serial(n);
    updated_bias1
        .split(v2617, v2617, v2617i, 8, TailStrategy::ShiftInwards)
        .unroll(v2617)
        .unroll(v2618)
        .vectorize(v2617i)
        .compute_root()
        .reorder(v2617i, v2617, v2618)
        .serial(v2618)
        .serial(v2617);
    updated_bias1.update(0)
        .split(v2617, v2617, v2617i, 8, TailStrategy::GuardWithIf)
        .unroll(v2617)
        .vectorize(v2617i)
        .reorder(v2617i, v2617)
        .serial(v2617);
    updated_bias1.update(1)
        .split(v2617, v2617, v2617i, 8, TailStrategy::GuardWithIf)
        .unroll(v2617)
        .vectorize(v2617i)
        .reorder(v2617i, v2617)
        .serial(v2617);
    updated_bias1.update(2)
        .split(v2617, v2617, v2617i, 8, TailStrategy::GuardWithIf)
        .unroll(v2617)
        .vectorize(v2617i)
        .reorder(v2617i, v2617)
        .serial(v2617);
    updated_bias1.update(3)
        .split(v2617, v2617, v2617i, 8, TailStrategy::GuardWithIf)
        .unroll(v2617)
        .vectorize(v2617i)
        .reorder(v2617i, v2617)
        .serial(v2617);
    constant_exterior_22
        .split(v320, v320, v320i, 8, TailStrategy::RoundUp)
        .vectorize(v320i)
        .compute_at(updated_bias1, v2617)
        .reorder(v320i, v320);
    repeat_edge_22
        .split(v320, v320, v320i, 8, TailStrategy::RoundUp)
        .vectorize(v320i)
        .compute_at(updated_bias1, v2617)
        .reorder(v320i, v320);
    bias1_im_0_d_def__
        .split(v320, v320, v320i, 8, TailStrategy::RoundUp)
        .vectorize(v320i)
        .compute_at(updated_bias1, v2617)
        .reorder(v320i, v320);
    bias1_im_0_d_def__.update(0)
        .split(v320, v320, v320i, 8, TailStrategy::RoundUp)
        .vectorize(v320i)
        .reorder(v320i, v320, r1111_x);
    conv1_stage1_0_d_def___1
        .split(c, c, ci, 8, TailStrategy::RoundUp)
        .unroll(w)
        .vectorize(ci)
        .compute_at(updated_bias1, v2617)
        .reorder(ci, c, w);
    updated_filter1
        .split(v2599, v2599, v2599i, 8, TailStrategy::ShiftInwards)
        .unroll(v2599)
        .vectorize(v2599i)
        .compute_root()
        .reorder(v2599i, v2599, v2600, v2601)
        .serial(v2601)
        .serial(v2600);
    updated_filter1.update(0)
        .split(v2599, v2599, v2599i, 8, TailStrategy::GuardWithIf)
        .split(v2600, v2600, v2600i, 4, TailStrategy::GuardWithIf)
        .split(v2599i, v2599i, v2599ii, 8, TailStrategy::GuardWithIf)
        .unroll(v2600i)
        .vectorize(v2599ii)
        .reorder(v2599ii, v2599i, v2600i, v2599, v2600)
        .serial(v2600)
        .serial(v2599);
    updated_filter1.update(1)
        .split(v2599, v2599, v2599i, 8, TailStrategy::GuardWithIf)
        .unroll(v2599)
        .vectorize(v2599i)
        .reorder(v2599i, v2599, v2600)
        .serial(v2600);
    updated_filter1.update(2)
        .split(v2599, v2599, v2599i, 8, TailStrategy::GuardWithIf)
        .unroll(v2599)
        .vectorize(v2599i)
        .reorder(v2599i, v2599, v2600)
        .serial(v2600);
    updated_filter1.update(3)
        .split(v2599, v2599, v2599i, 8, TailStrategy::GuardWithIf)
        .unroll(v2599)
        .vectorize(v2599i)
        .reorder(v2599i, v2599, v2600)
        .serial(v2600);
    constant_exterior_21
        .split(v324, v324, v324i, 8, TailStrategy::RoundUp)
        .unroll(v325)
        .vectorize(v324i)
        .compute_at(updated_filter1, v2599)
        .reorder(v324i, v324, v325);
    repeat_edge_21
        .split(v324, v324, v324i, 8, TailStrategy::RoundUp)
        .unroll(v325)
        .vectorize(v324i)
        .compute_at(updated_filter1, v2599)
        .reorder(v324i, v324, v325);
    filter1_im_0_d_def__
        .split(v324, v324, v324i, 8, TailStrategy::RoundUp)
        .unroll(v324)
        .vectorize(v324i)
        .compute_root()
        .reorder(v324i, v324, v325)
        .serial(v325);
    filter1_im_0_d_def__.update(0)
        .split(v324, v324, v324i, 8, TailStrategy::RoundUp)
        .unroll(v324)
        .vectorize(v324i)
        .reorder(v324i, v324, r895_x, r895_y, r895_z)
        .serial(r895_z);
    filter1_im_0_d_def__.update(1)
        .split(v324, v324, v324i, 8, TailStrategy::RoundUp)
        .split(v324i, v324i, v324ii, 8, TailStrategy::GuardWithIf)
        .unroll(v324i)
        .vectorize(v324ii)
        .reorder(v324ii, v324i, r1092_x, r1092_y, v324)
        .serial(v324)
        .serial(r1092_y);
    updated_head2_bias
        .split(v2587, v2587, v2587i, 8, TailStrategy::ShiftInwards)
        .vectorize(v2587i)
        .compute_root()
        .reorder(v2587i, v2587, v2588)
        .serial(v2588)
        .serial(v2587);
    updated_head2_bias.update(0)
        .split(v2587, v2587, v2587i, 8, TailStrategy::GuardWithIf)
        .unroll(v2587)
        .vectorize(v2587i)
        .reorder(v2587i, v2587)
        .serial(v2587);
    updated_head2_bias.update(1)
        .split(v2587, v2587, v2587i, 8, TailStrategy::GuardWithIf)
        .unroll(v2587)
        .vectorize(v2587i)
        .reorder(v2587i, v2587)
        .serial(v2587);
    updated_head2_bias.update(2)
        .split(v2587, v2587, v2587i, 8, TailStrategy::GuardWithIf)
        .unroll(v2587)
        .vectorize(v2587i)
        .reorder(v2587i, v2587)
        .serial(v2587);
    updated_head2_bias.update(3)
        .split(v2587, v2587, v2587i, 8, TailStrategy::GuardWithIf)
        .unroll(v2587)
        .vectorize(v2587i)
        .reorder(v2587i, v2587)
        .serial(v2587);
    constant_exterior_14
        .split(v372, v372, v372i, 8, TailStrategy::RoundUp)
        .vectorize(v372i)
        .compute_at(updated_head2_bias, v2587)
        .reorder(v372i, v372);
    repeat_edge_14
        .split(v372, v372, v372i, 8, TailStrategy::RoundUp)
        .unroll(v372)
        .vectorize(v372i)
        .compute_root()
        .reorder(v372i, v372)
        .serial(v372);
    head2_bias_im_0_d_def__
        .split(v372, v372, v372i, 8, TailStrategy::RoundUp)
        .vectorize(v372i)
        .compute_at(repeat_edge_14, v372)
        .reorder(v372i, v372);
    head2_bias_im_0_d_def__.update(0)
        .split(v372, v372, v372i, 8, TailStrategy::RoundUp)
        .vectorize(v372i)
        .reorder(v372i, v372, r999_x, r999_y);
    head2_conv_0_d_def___1
        .split(c, c, ci, 8, TailStrategy::RoundUp)
        .vectorize(ci)
        .compute_at(repeat_edge_14, v372)
        .reorder(ci, c, w, n);
    updated_head2_filter
        .split(v2569, v2569, v2569i, 8, TailStrategy::ShiftInwards)
        .unroll(v2569)
        .vectorize(v2569i)
        .compute_root()
        .reorder(v2569i, v2569, v2570, v2571)
        .serial(v2571)
        .serial(v2570);
    updated_head2_filter.update(0)
        .split(v2569, v2569, v2569i, 24, TailStrategy::GuardWithIf)
        .split(v2569i, v2569i, v2569ii, 8, TailStrategy::GuardWithIf)
        .unroll(v2569i)
        .vectorize(v2569ii)
        .reorder(v2569ii, v2569i, v2569, v2570)
        .serial(v2570)
        .serial(v2569);
    updated_head2_filter.update(1)
        .split(v2569, v2569, v2569i, 8, TailStrategy::GuardWithIf)
        .split(v2569i, v2569i, v2569ii, 8, TailStrategy::GuardWithIf)
        .unroll(v2569i)
        .vectorize(v2569ii)
        .reorder(v2569ii, v2569i, v2569, v2570)
        .serial(v2570)
        .serial(v2569);
    updated_head2_filter.update(2)
        .split(v2569, v2569, v2569i, 8, TailStrategy::GuardWithIf)
        .split(v2569i, v2569i, v2569ii, 8, TailStrategy::GuardWithIf)
        .unroll(v2569i)
        .vectorize(v2569ii)
        .reorder(v2569ii, v2569i, v2569, v2570)
        .serial(v2570)
        .serial(v2569);
    updated_head2_filter.update(3)
        .split(v2569, v2569, v2569i, 8, TailStrategy::GuardWithIf)
        .split(v2569i, v2569i, v2569ii, 8, TailStrategy::GuardWithIf)
        .unroll(v2569i)
        .vectorize(v2569ii)
        .reorder(v2569ii, v2569i, v2569, v2570)
        .serial(v2570)
        .serial(v2569);
    constant_exterior_13
        .store_in(MemoryType::Stack)
        .split(v376, v376, v376i, 8, TailStrategy::RoundUp)
        .vectorize(v376i)
        .compute_at(updated_head2_filter, v2569i)
        .reorder(v376i, v376, v377);
    repeat_edge_13
        .store_in(MemoryType::Stack)
        .split(v376, v376, v376i, 8, TailStrategy::RoundUp)
        .vectorize(v376i)
        .compute_at(updated_head2_filter, v2569i)
        .reorder(v376i, v376, v377);
    head2_filter_im_0_d_def__
        .split(v376, v376, v376i, 8, TailStrategy::RoundUp)
        .split(v376i, v376i, v376ii, 8, TailStrategy::RoundUp)
        .unroll(v376i)
        .vectorize(v376ii)
        .compute_root()
        .reorder(v376ii, v376i, v376, v377)
        .serial(v377)
        .serial(v376);
    head2_filter_im_0_d_def__.update(0)
        .split(v376, v376, v376i, 8, TailStrategy::RoundUp)
        .split(v376i, v376i, v376ii, 8, TailStrategy::GuardWithIf)
        .unroll(v376i)
        .vectorize(v376ii)
        .reorder(v376ii, v376i, r980_x, r980_y, r980_z, v376)
        .serial(v376)
        .serial(r980_z);
    constant_exterior_10
        .split(c, c, ci, 2, TailStrategy::RoundUp)
        .split(n, n, ni, 40, TailStrategy::RoundUp)
        .split(ni, ni, nii, 8, TailStrategy::RoundUp)
        .split(w, w, wi, 2, TailStrategy::RoundUp)
        .split(nii, nii, niii, 8, TailStrategy::RoundUp)
        .unroll(wi)
        .vectorize(niii)
        .compute_root()
        .reorder(niii, wi, nii, ci, w, ni, c, n)
        .serial(n)
        .serial(c)
        .reorder_storage(n, c, w);
    repeat_edge_10
        .store_in(MemoryType::Stack)
        .split(n, n, ni, 8, TailStrategy::RoundUp)
        .unroll(w)
        .vectorize(ni)
        .compute_at(constant_exterior_10, ci)
        .reorder(ni, c, w, n)
        .reorder_storage(n, c, w);
    head2_conv_1_d_def__
        .store_in(MemoryType::Stack)
        .split(n, n, ni, 8, TailStrategy::RoundUp)
        .vectorize(ni)
        .compute_at(constant_exterior_10, ni)
        .reorder(ni, c, w, n)
        .reorder_storage(n, c, w);
    constant_exterior_9
        .store_in(MemoryType::Stack)
        .split(n, n, ni, 8, TailStrategy::RoundUp)
        .vectorize(ni)
        .compute_at(constant_exterior_10, ni)
        .reorder(ni, c, w, n)
        .reorder_storage(n, c, w);
    repeat_edge_9
        .store_in(MemoryType::Stack)
        .split(n, n, ni, 8, TailStrategy::RoundUp)
        .vectorize(ni)
        .compute_at(constant_exterior_10, ni)
        .reorder(ni, c, w, n)
        .reorder_storage(n, c, w);
    head2_relu_0_d_def__
        .split(c, c, ci, 2, TailStrategy::ShiftInwards)
        .split(n, n, ni, 8, TailStrategy::ShiftInwards)
        .vectorize(ni)
        .compute_root()
        .reorder(ni, ci, n, c, w)
        .serial(w)
        .serial(c)
        .reorder_storage(n, c, w);
    head2_relu_0_d_def__.update(0)
        .split(r871_y, r871_y, r871_yi, 2, TailStrategy::GuardWithIf)
        .split(n, n, ni, 8, TailStrategy::GuardWithIf)
        .vectorize(ni)
        .reorder(ni, r871_x, r871_yi, n, r871_y, w)
        .serial(w)
        .serial(r871_y);
    updated_head1_bias
        .split(v2557, v2557, v2557i, 8, TailStrategy::ShiftInwards)
        .unroll(v2557)
        .unroll(v2558)
        .vectorize(v2557i)
        .compute_root()
        .reorder(v2557i, v2557, v2558)
        .serial(v2558)
        .serial(v2557);
    updated_head1_bias.update(0)
        .split(v2557, v2557, v2557i, 8, TailStrategy::GuardWithIf)
        .unroll(v2557)
        .vectorize(v2557i)
        .reorder(v2557i, v2557)
        .serial(v2557);
    updated_head1_bias.update(1)
        .split(v2557, v2557, v2557i, 8, TailStrategy::GuardWithIf)
        .unroll(v2557)
        .vectorize(v2557i)
        .reorder(v2557i, v2557)
        .serial(v2557);
    updated_head1_bias.update(2)
        .split(v2557, v2557, v2557i, 8, TailStrategy::GuardWithIf)
        .unroll(v2557)
        .vectorize(v2557i)
        .reorder(v2557i, v2557)
        .serial(v2557);
    updated_head1_bias.update(3)
        .split(v2557, v2557, v2557i, 8, TailStrategy::GuardWithIf)
        .unroll(v2557)
        .vectorize(v2557i)
        .reorder(v2557i, v2557)
        .serial(v2557);
    repeat_edge_20
        .split(v329, v329, v329i, 8, TailStrategy::RoundUp)
        .vectorize(v329i)
        .compute_at(updated_head1_bias, v2557)
        .reorder(v329i, v329);
    head1_bias_im_0_d_def__
        .split(v329, v329, v329i, 8, TailStrategy::RoundUp)
        .vectorize(v329i)
        .compute_at(updated_head1_bias, v2557)
        .reorder(v329i, v329);
    head1_bias_im_0_d_def__.update(0)
        .split(v329, v329, v329i, 8, TailStrategy::RoundUp)
        .vectorize(v329i)
        .reorder(v329i, v329, r1197_x);
    head1_conv_0_d_def___1
        .split(c, c, ci, 8, TailStrategy::RoundUp)
        .unroll(w)
        .vectorize(ci)
        .compute_at(updated_head1_bias, v2557)
        .reorder(ci, c, w);
    updated_head1_filter
        .split(v2534, v2534, v2534i, 4, TailStrategy::ShiftInwards)
        .split(v2533, v2533, v2533i, 8, TailStrategy::ShiftInwards)
        .unroll(v2533)
        .unroll(v2534i)
        .vectorize(v2533i)
        .compute_root()
        .reorder(v2533i, v2533, v2534i, v2534, v2535, v2536)
        .serial(v2536)
        .serial(v2535)
        .serial(v2534);
    updated_head1_filter.update(0)
        .split(v2533, v2533, v2533i, 8, TailStrategy::GuardWithIf)
        .split(v2534, v2534, v2534i, 28, TailStrategy::GuardWithIf)
        .split(v2533i, v2533i, v2533ii, 8, TailStrategy::GuardWithIf)
        .vectorize(v2533ii)
        .reorder(v2533ii, v2533i, v2534i, v2533, v2534, v2535)
        .serial(v2535)
        .serial(v2534)
        .serial(v2533);
    updated_head1_filter.update(1)
        .split(v2533, v2533, v2533i, 8, TailStrategy::GuardWithIf)
        .unroll(v2533)
        .vectorize(v2533i)
        .reorder(v2533i, v2533, v2534, v2535)
        .serial(v2535)
        .serial(v2534);
    updated_head1_filter.update(2)
        .split(v2533, v2533, v2533i, 8, TailStrategy::GuardWithIf)
        .unroll(v2533)
        .vectorize(v2533i)
        .reorder(v2533i, v2533, v2534, v2535)
        .serial(v2535)
        .serial(v2534);
    updated_head1_filter.update(3)
        .split(v2533, v2533, v2533i, 8, TailStrategy::GuardWithIf)
        .unroll(v2533)
        .vectorize(v2533i)
        .reorder(v2533i, v2533, v2534, v2535)
        .serial(v2535)
        .serial(v2534);
    constant_exterior_18
        .split(v343, v343, v343i, 8, TailStrategy::RoundUp)
        .vectorize(v343i)
        .compute_at(updated_head1_filter, v2533)
        .reorder(v343i, v343, v344, v345);
    repeat_edge_18
        .split(v343, v343, v343i, 8, TailStrategy::RoundUp)
        .vectorize(v343i)
        .compute_at(updated_head1_filter, v2533)
        .reorder(v343i, v343, v344, v345);
    head1_filter_im_0_d_def__
        .split(v343, v343, v343i, 8, TailStrategy::ShiftInwards)
        .split(v344, v344, v344i, 28, TailStrategy::ShiftInwards)
        .split(v343i, v343i, v343ii, 8, TailStrategy::ShiftInwards)
        .vectorize(v343ii)
        .compute_root()
        .reorder(v343ii, v343i, v344i, v343, v344, v345)
        .serial(v345)
        .serial(v344)
        .serial(v343);
    constant_exterior_17
        .split(c, c, ci, 8, TailStrategy::RoundUp)
        .vectorize(ci)
        .compute_at(head1_filter_im_0_d_def__, v343)
        .reorder(ci, c, w, n);
    repeat_edge_17
        .split(c, c, ci, 8, TailStrategy::RoundUp)
        .vectorize(ci)
        .compute_at(head1_filter_im_0_d_def__, v343)
        .reorder(ci, c, w, n);
    squashed_head1_filter_0_d_def__
        .split(c, c, ci, 8, TailStrategy::ShiftInwards)
        .unroll(c)
        .vectorize(ci)
        .compute_root()
        .reorder(ci, c, w, n)
        .serial(n)
        .serial(w);
    squashed_head1_filter_0_d_def__.update(0)
        .split(c, c, ci, 8, TailStrategy::GuardWithIf)
        .unroll(c)
        .vectorize(ci)
        .reorder(ci, c, r1178_x, r1178_y, r1178_z)
        .serial(r1178_z)
        .serial(r1178_y);
    constant_exterior_16
        .split(w, w, wi, 8, TailStrategy::RoundUp)
        .vectorize(wi)
        .compute_root()
        .reorder(wi, c, w)
        .serial(w)
        .serial(c)
        .reorder_storage(w, c);
    head1_conv_1_d_def__
        .split(w, w, wi, 8, TailStrategy::ShiftInwards)
        .vectorize(wi)
        .compute_root()
        .reorder(wi, c, w)
        .serial(w)
        .serial(c)
        .reorder_storage(w, c);
    head1_conv_1_d_def__.update(0)
        .split(w, w, wi, 8, TailStrategy::GuardWithIf)
        .split(wi, wi, wii, 8, TailStrategy::GuardWithIf)
        .vectorize(wii)
        .reorder(wii, wi, r1068_x, r1068_y, w)
        .serial(w)
        .serial(r1068_y);
    constant_exterior_15
        .split(c, c, ci, 8, TailStrategy::RoundUp)
        .vectorize(ci)
        .compute_root()
        .reorder(ci, c, w)
        .serial(w)
        .serial(c);
    repeat_edge_15
        .split(c, c, ci, 8, TailStrategy::RoundUp)
        .vectorize(ci)
        .compute_at(constant_exterior_15, c)
        .reorder(ci, c, w);
    conv1_stage1_1_d_def__
        .split(c, c, ci, 8, TailStrategy::RoundUp)
        .vectorize(ci)
        .compute_at(constant_exterior_15, c)
        .reorder(ci, c, w);
    conv1_stage1_1_d_def__.update(0)
        .split(c, c, ci, 8, TailStrategy::RoundUp)
        .vectorize(ci)
        .reorder(ci, c, w, r914_x);
    conv1_stage2_0_d_def___1
        .split(c, c, ci, 8, TailStrategy::RoundUp)
        .vectorize(ci)
        .compute_at(constant_exterior_15, c)
        .reorder(ci, c, w, n);
    constant_exterior_8
        .split(n, n, ni, 16, TailStrategy::RoundUp)
        .split(ni, ni, nii, 8, TailStrategy::RoundUp)
        .unroll(ni)
        .vectorize(nii)
        .compute_root()
        .reorder(nii, ni, n, c, w)
        .serial(w)
        .serial(c)
        .reorder_storage(n, c, w);
    repeat_edge_8
        .store_in(MemoryType::Stack)
        .split(n, n, ni, 8, TailStrategy::RoundUp)
        .unroll(n)
        .vectorize(ni)
        .compute_at(constant_exterior_8, n)
        .reorder(ni, c, w, n)
        .reorder_storage(n, c, w);
    conv1_stage2_1_d_def__
        .split(n, n, ni, 8, TailStrategy::RoundUp)
        .unroll(n)
        .vectorize(ni)
        .compute_root()
        .reorder(ni, n, c, w)
        .serial(w)
        .serial(c)
        .reorder_storage(n, c, w);
    conv1_stage2_1_d_def__.update(0)
        .split(n, n, ni, 8, TailStrategy::RoundUp)
        .unroll(n)
        .vectorize(ni)
        .reorder(ni, n, r227_x, r227_y)
        .serial(r227_y)
        .serial(r227_x);
    conv1_stage2_1_d_def__.update(1)
        .split(c, c, ci, 1, TailStrategy::RoundUp)
        .split(n, n, ni, 16, TailStrategy::RoundUp)
        .split(w, w, wi, 2, TailStrategy::RoundUp)
        .split(ni, ni, nii, 8, TailStrategy::GuardWithIf)
        .split(nii, nii, niii, 8, TailStrategy::GuardWithIf)
        .unroll(wi)
        .vectorize(niii)
        .reorder(niii, wi, nii, ci, w, ni, c, n)
        .serial(n)
        .serial(c);
    constant_exterior_7
        .store_in(MemoryType::Stack)
        .split(n, n, ni, 8, TailStrategy::RoundUp)
        .unroll(w)
        .vectorize(ni)
        .compute_at(conv1_stage2_1_d_def__, ci)
        .reorder(ni, c, w, n)
        .reorder_storage(n, c, w);
    repeat_edge_7
        .split(n, n, ni, 8, TailStrategy::RoundUp)
        .vectorize(ni)
        .compute_at(conv1_stage2_1_d_def__, c)
        .reorder(ni, c, w, n)
        .reorder_storage(n, c, w);
    relu1_0_d_def__
        .split(n, n, ni, 8, TailStrategy::ShiftInwards)
        .unroll(n)
        .vectorize(ni)
        .compute_root()
        .reorder(ni, n, c, w)
        .serial(w)
        .serial(c)
        .reorder_storage(n, c, w);
    relu1_0_d_def__.update(0)
        .split(n, n, ni, 27, TailStrategy::GuardWithIf)
        .split(ni, ni, nii, 8, TailStrategy::GuardWithIf)
        .unroll(ni)
        .vectorize(nii)
        .reorder(nii, ni, w, n)
        .serial(n)
        .serial(w);
    relu1_0_d_def__.update(1)
        .split(n, n, ni, 27, TailStrategy::GuardWithIf)
        .split(ni, ni, nii, 8, TailStrategy::GuardWithIf)
        .unroll(ni)
        .vectorize(nii)
        .reorder(nii, ni, w, n)
        .serial(n)
        .serial(w);
    relu1_0_d_def__.update(2)
        .split(n, n, ni, 27, TailStrategy::GuardWithIf)
        .split(ni, ni, nii, 8, TailStrategy::GuardWithIf)
        .unroll(ni)
        .vectorize(nii)
        .reorder(nii, ni, w, n)
        .serial(n)
        .serial(w);
    relu1_0_d_def__.update(3)
        .split(n, n, ni, 27, TailStrategy::GuardWithIf)
        .split(ni, ni, nii, 8, TailStrategy::GuardWithIf)
        .unroll(ni)
        .vectorize(nii)
        .reorder(nii, ni, w, n)
        .serial(n)
        .serial(w);
    relu1_0_d_def__.update(4)
        .split(n, n, ni, 27, TailStrategy::GuardWithIf)
        .split(ni, ni, nii, 8, TailStrategy::GuardWithIf)
        .unroll(ni)
        .vectorize(nii)
        .reorder(nii, ni, w, n)
        .serial(n)
        .serial(w);
    relu1_0_d_def__.update(5)
        .split(n, n, ni, 27, TailStrategy::GuardWithIf)
        .split(ni, ni, nii, 8, TailStrategy::GuardWithIf)
        .unroll(ni)
        .vectorize(nii)
        .reorder(nii, ni, w, n)
        .serial(n)
        .serial(w);
    relu1_0_d_def__.update(6)
        .split(n, n, ni, 27, TailStrategy::GuardWithIf)
        .split(ni, ni, nii, 8, TailStrategy::GuardWithIf)
        .unroll(ni)
        .vectorize(nii)
        .reorder(nii, ni, w, n)
        .serial(n)
        .serial(w);
    relu1_0_d_def__.update(7)
        .split(n, n, ni, 27, TailStrategy::GuardWithIf)
        .split(ni, ni, nii, 8, TailStrategy::GuardWithIf)
        .unroll(ni)
        .vectorize(nii)
        .reorder(nii, ni, w, n)
        .serial(n)
        .serial(w);
    relu1_0_d_def__.update(8)
        .split(n, n, ni, 27, TailStrategy::GuardWithIf)
        .split(ni, ni, nii, 8, TailStrategy::GuardWithIf)
        .unroll(ni)
        .vectorize(nii)
        .reorder(nii, ni, w, n)
        .serial(n)
        .serial(w);
    relu1_0_d_def__.update(9)
        .split(n, n, ni, 27, TailStrategy::GuardWithIf)
        .split(ni, ni, nii, 8, TailStrategy::GuardWithIf)
        .unroll(ni)
        .vectorize(nii)
        .reorder(nii, ni, w, n)
        .serial(n)
        .serial(w);
    relu1_0_d_def__.update(10)
        .split(n, n, ni, 27, TailStrategy::GuardWithIf)
        .split(ni, ni, nii, 8, TailStrategy::GuardWithIf)
        .unroll(ni)
        .vectorize(nii)
        .reorder(nii, ni, w, n)
        .serial(n)
        .serial(w);
    relu1_0_d_def__.update(11)
        .split(n, n, ni, 27, TailStrategy::GuardWithIf)
        .split(ni, ni, nii, 8, TailStrategy::GuardWithIf)
        .unroll(ni)
        .vectorize(nii)
        .reorder(nii, ni, w, n)
        .serial(n)
        .serial(w);
    relu1_0_d_def__.update(12)
        .split(n, n, ni, 27, TailStrategy::GuardWithIf)
        .split(ni, ni, nii, 8, TailStrategy::GuardWithIf)
        .unroll(ni)
        .vectorize(nii)
        .reorder(nii, ni, w, n)
        .serial(n)
        .serial(w);
    relu1_0_d_def__.update(13)
        .split(n, n, ni, 27, TailStrategy::GuardWithIf)
        .split(ni, ni, nii, 8, TailStrategy::GuardWithIf)
        .unroll(ni)
        .vectorize(nii)
        .reorder(nii, ni, w, n)
        .serial(n)
        .serial(w);
    relu1_0_d_def__.update(14)
        .split(n, n, ni, 27, TailStrategy::GuardWithIf)
        .split(ni, ni, nii, 8, TailStrategy::GuardWithIf)
        .unroll(ni)
        .vectorize(nii)
        .reorder(nii, ni, w, n)
        .serial(n)
        .serial(w);
    relu1_0_d_def__.update(15)
        .split(n, n, ni, 27, TailStrategy::GuardWithIf)
        .split(ni, ni, nii, 8, TailStrategy::GuardWithIf)
        .unroll(ni)
        .vectorize(nii)
        .reorder(nii, ni, w, n)
        .serial(n)
        .serial(w);
    relu1_0_d_def__.update(16)
        .split(n, n, ni, 27, TailStrategy::GuardWithIf)
        .split(ni, ni, nii, 8, TailStrategy::GuardWithIf)
        .unroll(ni)
        .vectorize(nii)
        .reorder(nii, ni, w, n)
        .serial(n)
        .serial(w);
    relu1_0_d_def__.update(17)
        .split(n, n, ni, 27, TailStrategy::GuardWithIf)
        .split(ni, ni, nii, 8, TailStrategy::GuardWithIf)
        .unroll(ni)
        .vectorize(nii)
        .reorder(nii, ni, w, n)
        .serial(n)
        .serial(w);
    relu1_0_d_def__.update(18)
        .split(n, n, ni, 27, TailStrategy::GuardWithIf)
        .split(ni, ni, nii, 8, TailStrategy::GuardWithIf)
        .unroll(ni)
        .vectorize(nii)
        .reorder(nii, ni, w, n)
        .serial(n)
        .serial(w);
    relu1_0_d_def__.update(19)
        .split(n, n, ni, 27, TailStrategy::GuardWithIf)
        .split(ni, ni, nii, 8, TailStrategy::GuardWithIf)
        .unroll(ni)
        .vectorize(nii)
        .reorder(nii, ni, w, n)
        .serial(n)
        .serial(w);
    relu1_0_d_def__.update(20)
        .split(n, n, ni, 27, TailStrategy::GuardWithIf)
        .split(ni, ni, nii, 8, TailStrategy::GuardWithIf)
        .unroll(ni)
        .vectorize(nii)
        .reorder(nii, ni, w, n)
        .serial(n)
        .serial(w);
    relu1_0_d_def__.update(21)
        .split(n, n, ni, 27, TailStrategy::GuardWithIf)
        .split(ni, ni, nii, 8, TailStrategy::GuardWithIf)
        .unroll(ni)
        .vectorize(nii)
        .reorder(nii, ni, w, n)
        .serial(n)
        .serial(w);
    relu1_0_d_def__.update(22)
        .split(n, n, ni, 27, TailStrategy::GuardWithIf)
        .split(ni, ni, nii, 8, TailStrategy::GuardWithIf)
        .unroll(ni)
        .vectorize(nii)
        .reorder(nii, ni, w, n)
        .serial(n)
        .serial(w);
    relu1_0_d_def__.update(23)
        .split(n, n, ni, 27, TailStrategy::GuardWithIf)
        .split(ni, ni, nii, 8, TailStrategy::GuardWithIf)
        .unroll(ni)
        .vectorize(nii)
        .reorder(nii, ni, w, n)
        .serial(n)
        .serial(w);
    constant_exterior_6
        .split(n, n, ni, 16, TailStrategy::RoundUp)
        .split(w, w, wi, 2, TailStrategy::RoundUp)
        .split(ni, ni, nii, 8, TailStrategy::RoundUp)
        .split(nii, nii, niii, 8, TailStrategy::RoundUp)
        .unroll(wi)
        .vectorize(niii)
        .compute_root()
        .reorder(niii, nii, wi, ni, n, w)
        .serial(w)
        .serial(n);
    repeat_edge_6
        .store_in(MemoryType::Stack)
        .split(n, n, ni, 8, TailStrategy::RoundUp)
        .unroll(w)
        .vectorize(ni)
        .compute_at(constant_exterior_6, ni)
        .reorder(ni, n, w);
    f0_0_d_def__
        .split(n, n, ni, 27, TailStrategy::RoundUp)
        .split(ni, ni, nii, 8, TailStrategy::RoundUp)
        .unroll(ni)
        .vectorize(nii)
        .compute_root()
        .reorder(nii, ni, n, w)
        .serial(w)
        .serial(n);
    f0_0_d_def__.update(0)
        .split(n, n, ni, 27, TailStrategy::RoundUp)
        .split(ni, ni, nii, 8, TailStrategy::GuardWithIf)
        .unroll(ni)
        .vectorize(nii)
        .reorder(nii, ni, r255_x, n)
        .serial(n)
        .serial(r255_x);
    constant_exterior_5
        .split(n, n, ni, 8, TailStrategy::RoundUp)
        .unroll(n)
        .vectorize(ni)
        .compute_root()
        .reorder(ni, n)
        .serial(n);
    repeat_edge_5
        .split(n, n, ni, 8, TailStrategy::RoundUp)
        .vectorize(ni)
        .compute_at(constant_exterior_5, n)
        .reorder(ni, n);
    f1_1_d_def__
        .split(n, n, ni, 8, TailStrategy::ShiftInwards)
        .unroll(n)
        .vectorize(ni)
        .compute_root()
        .reorder(ni, n)
        .serial(n);
    f1_1_d_def__.update(1)
        .split(n, n, ni, 8, TailStrategy::GuardWithIf)
        .unroll(n)
        .vectorize(ni)
        .reorder(ni, n)
        .serial(n);
    f1
        .split(n, n, ni, 8, TailStrategy::RoundUp)
        .unroll(n)
        .vectorize(ni)
        .compute_root()
        .reorder(ni, n)
        .serial(n);
    f1.update(0)
        .split(n, n, ni, 8, TailStrategy::GuardWithIf)
        .split(ni, ni, nii, 8, TailStrategy::GuardWithIf)
        .vectorize(nii)
        .reorder(nii, ni, r24_x, n)
        .serial(n);
    f0
        .store_in(MemoryType::Stack)
        .split(n, n, ni, 8, TailStrategy::ShiftInwards)
        .vectorize(ni)
        .compute_at(f1, ni)
        .reorder(ni, n, w);
    conv1_stage2
        .split(c, c, ci, 8, TailStrategy::ShiftInwards)
        .split(n, n, ni, 40, TailStrategy::ShiftInwards)
        .split(ci, ci, cii, 8, TailStrategy::ShiftInwards)
        .split(cii, cii, ciii, 8, TailStrategy::ShiftInwards)
        .vectorize(ciii)
        .compute_root()
        .reorder(ciii, cii, ni, ci, c, w, n)
        .serial(n)
        .serial(w)
        .serial(c);
    conv1_stage2.update(0)
        .split(w, w, wi, 4, TailStrategy::GuardWithIf)
        .split(c, c, ci, 8, TailStrategy::GuardWithIf)
        .unroll(c)
        .unroll(wi)
        .vectorize(ci)
        .reorder(ci, c, wi, r19_x, w, n)
        .serial(n)
        .serial(w);
    head2_relu
        .split(c, c, ci, 2, TailStrategy::RoundUp)
        .split(n, n, ni, 8, TailStrategy::RoundUp)
        .vectorize(ni)
        .compute_root()
        .reorder(ni, ci, n, c, w)
        .serial(w)
        .serial(c)
        .reorder_storage(n, c, w);
    head2_conv
        .split(w, w, wi, 4, TailStrategy::ShiftInwards)
        .split(c, c, ci, 8, TailStrategy::ShiftInwards)
        .vectorize(ci)
        .compute_root()
        .reorder(ci, c, wi, w, n)
        .serial(n)
        .serial(w);
    head2_conv.update(0)
        .split(w, w, wi, 4, TailStrategy::GuardWithIf)
        .split(c, c, ci, 8, TailStrategy::GuardWithIf)
        .vectorize(ci)
        .reorder(ci, r9_x, c, wi, w, n)
        .serial(n)
        .serial(w);
    normalized_schedule_features
        .split(n, n, ni, 8, TailStrategy::ShiftInwards)
        .unroll(n)
        .vectorize(ni)
        .compute_root()
        .reorder(ni, n, c, s)
        .serial(s)
        .serial(c);
    conv1_stage1
        .store_in(MemoryType::Stack)
        .split(c, c, ci, 8, TailStrategy::ShiftInwards)
        .vectorize(ci)
        .compute_at(conv1_stage2, ci)
        .reorder(ci, c, w);
    conv1_stage1.update(0)
        .split(c, c, ci, 8, TailStrategy::GuardWithIf)
        .vectorize(ci)
        .reorder(ci, c, w, r14_x);
    head1_conv
        .split(c, c, ci, 8, TailStrategy::ShiftInwards)
        .vectorize(ci)
        .compute_root()
        .reorder(ci, c, w)
        .serial(w)
        .serial(c);
    head1_conv.update(0)
        .split(c, c, ci, 8, TailStrategy::GuardWithIf)
        .split(ci, ci, cii, 8, TailStrategy::GuardWithIf)
        .vectorize(cii)
        .reorder(cii, ci, r4_x, r4_y, c, w)
        .serial(w)
        .serial(c);
    squashed_head1_filter
        .split(w, w, wi, 8, TailStrategy::ShiftInwards)
        .unroll(w)
        .vectorize(wi)
        .compute_root()
        .reorder(wi, w, c, n)
        .serial(n)
        .serial(c)
        .reorder_storage(w, c, n);
    constant_exterior_4
        .split(n, n, ni, 8, TailStrategy::RoundUp)
        .unroll(n)
        .vectorize(ni)
        .compute_root()
        .reorder(ni, n)
        .serial(n);
    repeat_edge_4
        .split(n, n, ni, 8, TailStrategy::RoundUp)
        .unroll(n)
        .vectorize(ni)
        .compute_root()
        .reorder(ni, n)
        .serial(n);
    sum_1_d_def__
        .split(n, n, ni, 8, TailStrategy::RoundUp)
        .vectorize(ni)
        .compute_at(repeat_edge_4, n)
        .reorder(ni, n);
    constant_exterior_2
        .split(n, n, ni, 8, TailStrategy::RoundUp)
        .unroll(n)
        .vectorize(ni)
        .compute_root()
        .reorder(ni, n)
        .serial(n);
    repeat_edge_2
        .split(n, n, ni, 8, TailStrategy::RoundUp)
        .vectorize(ni)
        .compute_at(constant_exterior_2, n)
        .reorder(ni, n);
    f2_0_d_def__
        .split(n, n, ni, 8, TailStrategy::RoundUp)
        .unroll(n)
        .vectorize(ni)
        .compute_root()
        .reorder(ni, n)
        .serial(n);
    constant_exterior_1
        .compute_root();
}
