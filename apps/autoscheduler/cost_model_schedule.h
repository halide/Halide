// MACHINE-GENERATED - DO NOT EDIT

#include "Halide.h"

inline void do_cost_model_schedule(Halide::Pipeline p) {
  using namespace Halide;
  Func loss_output = p.get_func(116);
  Func sum_2 = p.get_func(115);
  Func f2 = p.get_func(114);
  Func sum = p.get_func(113);
  Func sum_1 = p.get_func(112);
  Func prediction_output = p.get_func(111);
  Func updated_bias1 = p.get_func(110);
  Func constant_exterior_34 = p.get_func(109);
  Func repeat_edge_34 = p.get_func(108);
  Func bias1_im_0_d_def__ = p.get_func(107);
  Func conv1_stage1_0_d_def___1 = p.get_func(106);
  Func updated_filter1 = p.get_func(105);
  Func constant_exterior_23 = p.get_func(104);
  Func repeat_edge_23 = p.get_func(103);
  Func filter1_im_0_d_def__ = p.get_func(102);
  Func updated_head2_bias = p.get_func(101);
  Func constant_exterior_20 = p.get_func(100);
  Func repeat_edge_20 = p.get_func(99);
  Func head2_bias_im_0_d_def__ = p.get_func(98);
  Func head2_conv_0_d_def___1 = p.get_func(97);
  Func updated_head2_filter = p.get_func(96);
  Func constant_exterior_19 = p.get_func(95);
  Func repeat_edge_19 = p.get_func(94);
  Func head2_filter_im_0_d_def__ = p.get_func(93);
  Func constant_exterior_14 = p.get_func(92);
  Func repeat_edge_14 = p.get_func(91);
  Func head2_conv_1_d_def__ = p.get_func(90);
  Func constant_exterior_13 = p.get_func(89);
  Func repeat_edge_13 = p.get_func(88);
  Func head2_relu_0_d_def__ = p.get_func(87);
  Func constant_exterior_12 = p.get_func(86);
  Func repeat_edge_12 = p.get_func(85);
  Func repeat_edge_1_0_d_def__ = p.get_func(84);
  Func constant_exterior_11 = p.get_func(83);
  Func repeat_edge_11 = p.get_func(82);
  Func constant_exterior_1_0_d_def__ = p.get_func(81);
  Func updated_head1_bias = p.get_func(80);
  Func constant_exterior_33 = p.get_func(79);
  Func repeat_edge_33 = p.get_func(78);
  Func head1_bias_im_0_d_def__ = p.get_func(77);
  Func head1_conv_0_d_def___1 = p.get_func(76);
  Func updated_head1_filter = p.get_func(75);
  Func constant_exterior_32 = p.get_func(74);
  Func repeat_edge_32 = p.get_func(73);
  Func head1_filter_im_0_d_def__ = p.get_func(72);
  Func constant_exterior_27 = p.get_func(71);
  Func repeat_edge_27 = p.get_func(70);
  Func head1_conv_1_d_def__ = p.get_func(69);
  Func constant_exterior_26 = p.get_func(68);
  Func repeat_edge_26 = p.get_func(67);
  Func head1_relu_0_d_def__ = p.get_func(66);
  Func constant_exterior_25 = p.get_func(65);
  Func repeat_edge_25 = p.get_func(64);
  Func repeat_edge_0_d_def__ = p.get_func(63);
  Func constant_exterior_24 = p.get_func(62);
  Func repeat_edge_24 = p.get_func(61);
  Func constant_exterior_0_d_def__ = p.get_func(60);
  Func constant_exterior_22 = p.get_func(59);
  Func repeat_edge_22 = p.get_func(58);
  Func conv1_stage1_1_d_def__ = p.get_func(57);
  Func conv1_stage2_0_d_def___1 = p.get_func(56);
  Func constant_exterior_10 = p.get_func(55);
  Func repeat_edge_10 = p.get_func(54);
  Func conv1_stage2_1_d_def__ = p.get_func(53);
  Func constant_exterior_9 = p.get_func(52);
  Func repeat_edge_9 = p.get_func(51);
  Func relu1_0_d_def__ = p.get_func(50);
  Func constant_exterior_8 = p.get_func(49);
  Func repeat_edge_8 = p.get_func(48);
  Func f0_0_d_def__ = p.get_func(47);
  Func constant_exterior_7 = p.get_func(46);
  Func repeat_edge_7 = p.get_func(45);
  Func f1_1_d_def__ = p.get_func(44);
  Func true_runtime_im = p.get_func(43);
  Func f1 = p.get_func(42);
  Func f0 = p.get_func(41);
  Func relu1 = p.get_func(40);
  Func conv1_stage2 = p.get_func(39);
  Func constant_exterior_1 = p.get_func(38);
  Func repeat_edge_1 = p.get_func(37);
  Func head2_relu = p.get_func(36);
  Func head2_conv = p.get_func(35);
  Func normalized_schedule_features = p.get_func(34);
  Func schedule_std_im = p.get_func(33);
  Func schedule_mean_im = p.get_func(32);
  Func schedule_features_im = p.get_func(31);
  Func head2_filter_im = p.get_func(30);
  Func head2_bias_im = p.get_func(29);
  Func constant_exterior_6 = p.get_func(28);
  Func repeat_edge_6 = p.get_func(27);
  Func sum_1_d_def__ = p.get_func(26);
  Func constant_exterior_4 = p.get_func(25);
  Func repeat_edge_4 = p.get_func(24);
  Func f2_0_d_def__ = p.get_func(23);
  Func constant_exterior_3 = p.get_func(22);
  Func repeat_edge_3 = p.get_func(21);
  Func sum_2_1_d_def__ = p.get_func(20);
  Func conv1_stage1 = p.get_func(19);
  Func filter1_im = p.get_func(18);
  Func constant_exterior = p.get_func(17);
  Func repeat_edge = p.get_func(16);
  Func head1_relu = p.get_func(15);
  Func head1_conv = p.get_func(14);
  Func normalized_pipeline_features = p.get_func(13);
  Func pipeline_std_im = p.get_func(12);
  Func pipeline_mean_im = p.get_func(11);
  Func pipeline_features_im = p.get_func(10);
  Func head1_filter_im = p.get_func(9);
  Func head1_bias_im = p.get_func(8);
  Func bias1_im = p.get_func(7);
  Func constant_exterior_21 = p.get_func(6);
  Func repeat_edge_21 = p.get_func(5);
  Func sum_1_1_d_def__ = p.get_func(4);
  Func constant_exterior_2 = p.get_func(3);
  Func repeat_edge_2 = p.get_func(2);
  Func loss_output_0_d_def__ = p.get_func(1);
  Func adjoint = p.get_func(0);
  Var _0(true_runtime_im.get_schedule().dims()[0].var), _0_vec("_0_vec"), _1(schedule_features_im.get_schedule().dims()[1].var), _2(schedule_features_im.get_schedule().dims()[2].var), c(repeat_edge_12.get_schedule().dims()[0].var), c_vec("c_vec"), ci("ci"), ci_vec("ci_vec"), cii("cii"), cii_vec("cii_vec"), j(normalized_pipeline_features.get_schedule().dims()[1].var), ji("ji"), n(prediction_output.get_schedule().dims()[0].var), n_vec("n_vec"), ni("ni"), ni_vec("ni_vec"), r1000_x(conv1_stage1_1_d_def__.update(1).get_schedule().dims()[0].var), r1000_xi("r1000$xi"), r1000_xii("r1000$xii"), r1000_y(conv1_stage1_1_d_def__.update(1).get_schedule().dims()[1].var), r1000_yi("r1000$yi"), r1033_y(constant_exterior_0_d_def__.update(0).get_schedule().dims()[1].var), r1062_y(filter1_im_0_d_def__.update(1).get_schedule().dims()[1].var), r1062_z(filter1_im_0_d_def__.update(1).get_schedule().dims()[2].var), r117_x(f2_0_d_def__.update(0).get_schedule().dims()[0].var), r117_xi("r117$xi"), r1194_y(head1_filter_im_0_d_def__.update(0).get_schedule().dims()[1].var), r1194_z(head1_filter_im_0_d_def__.update(0).get_schedule().dims()[2].var), r210_x(conv1_stage2_1_d_def__.update(0).get_schedule().dims()[0].var), r210_xi("r210$xi"), r210_y(conv1_stage2_1_d_def__.update(0).get_schedule().dims()[1].var), r238_x(f0_0_d_def__.update(0).get_schedule().dims()[0].var), r714_y(constant_exterior_1_0_d_def__.update(0).get_schedule().dims()[1].var), r743_w(filter1_im_0_d_def__.update(0).get_schedule().dims()[3].var), r743_z(filter1_im_0_d_def__.update(0).get_schedule().dims()[2].var), r856_z(head2_filter_im_0_d_def__.update(0).get_schedule().dims()[2].var), s(normalized_schedule_features.get_schedule().dims()[2].var), si("si"), sii("sii"), v2860(updated_head1_filter.get_schedule().dims()[0].var), v2860_vec("v2860_vec"), v2860i("v2860i"), v2860i_vec("v2860i_vec"), v2861(updated_head1_filter.get_schedule().dims()[1].var), v2861i("v2861i"), v2862(updated_head1_filter.get_schedule().dims()[2].var), v2862i("v2862i"), v2863(updated_head1_filter.get_schedule().dims()[3].var), v2884(updated_head1_bias.get_schedule().dims()[0].var), v2884_vec("v2884_vec"), v2885(updated_head1_bias.get_schedule().dims()[1].var), v2896(updated_head2_filter.get_schedule().dims()[0].var), v2896_vec("v2896_vec"), v2896i("v2896i"), v2896i_vec("v2896i_vec"), v2897(updated_head2_filter.get_schedule().dims()[1].var), v2897i("v2897i"), v2898(updated_head2_filter.get_schedule().dims()[2].var), v2914(updated_head2_bias.get_schedule().dims()[0].var), v2914_vec("v2914_vec"), v2915(updated_head2_bias.get_schedule().dims()[1].var), v2926(updated_filter1.get_schedule().dims()[0].var), v2926_vec("v2926_vec"), v2927(updated_filter1.get_schedule().dims()[1].var), v2927i("v2927i"), v2928(updated_filter1.get_schedule().dims()[2].var), v2929(updated_filter1.get_schedule().dims()[3].var), v2950(updated_bias1.get_schedule().dims()[0].var), v2950_vec("v2950_vec"), v2951(updated_bias1.get_schedule().dims()[1].var), v458(bias1_im_0_d_def__.get_schedule().dims()[0].var), v458_vec("v458_vec"), v461(constant_exterior_33.get_schedule().dims()[0].var), v461_vec("v461_vec"), v466(constant_exterior_32.get_schedule().dims()[0].var), v466_vec("v466_vec"), v467(constant_exterior_32.get_schedule().dims()[1].var), v468(constant_exterior_32.get_schedule().dims()[2].var), v522(repeat_edge_23.get_schedule().dims()[0].var), v522_vec("v522_vec"), v523(repeat_edge_23.get_schedule().dims()[1].var), v524(repeat_edge_23.get_schedule().dims()[2].var), v537(constant_exterior_20.get_schedule().dims()[0].var), v537_vec("v537_vec"), v541(constant_exterior_19.get_schedule().dims()[0].var), v541_vec("v541_vec"), v542(constant_exterior_19.get_schedule().dims()[1].var), w(repeat_edge_12.get_schedule().dims()[1].var), wi("wi");
  RVar r1033_x(constant_exterior_0_d_def__.update(0).get_schedule().dims()[0].var), r1033_z(constant_exterior_0_d_def__.update(0).get_schedule().dims()[2].var), r1062_x(filter1_im_0_d_def__.update(1).get_schedule().dims()[0].var), r1081_x(bias1_im_0_d_def__.update(0).get_schedule().dims()[0].var), r1118_x(head1_relu_0_d_def__.update(0).get_schedule().dims()[0].var), r1194_x(head1_filter_im_0_d_def__.update(0).get_schedule().dims()[0].var), r1213_x(head1_bias_im_0_d_def__.update(0).get_schedule().dims()[0].var), r14_x(conv1_stage1.update(0).get_schedule().dims()[0].var), r14_y(conv1_stage1.update(0).get_schedule().dims()[1].var), r19_x(conv1_stage2.update(0).get_schedule().dims()[0].var), r19_y(conv1_stage2.update(0).get_schedule().dims()[1].var), r24_x(f1.update(0).get_schedule().dims()[0].var), r29_x(sum_2.update(0).get_schedule().dims()[0].var), r34_x(sum.update(0).get_schedule().dims()[0].var), r34_y(sum.update(0).get_schedule().dims()[1].var), r4_x(head1_conv.update(0).get_schedule().dims()[0].var), r4_y(head1_conv.update(0).get_schedule().dims()[1].var), r714_x(constant_exterior_1_0_d_def__.update(0).get_schedule().dims()[0].var), r714_z(constant_exterior_1_0_d_def__.update(0).get_schedule().dims()[2].var), r743_x(filter1_im_0_d_def__.update(0).get_schedule().dims()[0].var), r743_y(filter1_im_0_d_def__.update(0).get_schedule().dims()[1].var), r762_x(conv1_stage1_1_d_def__.update(0).get_schedule().dims()[0].var), r790_x(head2_relu_0_d_def__.update(0).get_schedule().dims()[0].var), r856_x(head2_filter_im_0_d_def__.update(0).get_schedule().dims()[0].var), r856_y(head2_filter_im_0_d_def__.update(0).get_schedule().dims()[1].var), r875_x(head2_bias_im_0_d_def__.update(0).get_schedule().dims()[0].var), r875_y(head2_bias_im_0_d_def__.update(0).get_schedule().dims()[1].var), r9_x(head2_conv.update(0).get_schedule().dims()[0].var);
  repeat_edge_23
      .store_in(MemoryType::Stack)
      .split(v522, v522, v522_vec, 8, TailStrategy::RoundUp).vectorize(v522_vec)
      .unroll(v522)
      .unroll(v523)
      .unroll(v524)
      .compute_at(updated_filter1, v2928)
      .store_at(updated_filter1, v2927)
      .reorder(v522_vec, v522, v523, v524);
  prediction_output
      .split(n, n, n_vec, 8, TailStrategy::ShiftInwards).vectorize(n_vec)
      .unroll(n)
      .compute_root()
      .serial(n)
      .reorder(n_vec, n);
  constant_exterior_23
      .store_in(MemoryType::Stack)
      .split(v522, v522, v522_vec, 8, TailStrategy::RoundUp).vectorize(v522_vec)
      .unroll(v522)
      .unroll(v523)
      .unroll(v524)
      .compute_at(updated_filter1, v2928)
      .store_at(updated_filter1, v2927)
      .reorder(v522_vec, v522, v523, v524);
  repeat_edge_12
      .store_in(MemoryType::Stack)
      .split(c, c, c_vec, 8, TailStrategy::RoundUp).vectorize(c_vec)
      .compute_at(constant_exterior_14, ni)
      .store_at(constant_exterior_14, w)
      .reorder(c_vec, c, w, n);
  constant_exterior_14
      .split(w, w, wi, 7, TailStrategy::RoundUp)
      .split(n, n, ni, 8, TailStrategy::RoundUp)
      .split(c, c, c_vec, 8, TailStrategy::RoundUp).vectorize(c_vec)
      .compute_root()
      .serial(n)
      .serial(w)
      .reorder(c_vec, c, wi, ni, w, n);
  sum_2
      .compute_root();
  sum_2.update(0);
  head2_conv_1_d_def__
      .store_in(MemoryType::Stack)
      .split(c, c, c_vec, 8, TailStrategy::RoundUp).vectorize(c_vec)
      .compute_at(constant_exterior_14, ni)
      .store_at(constant_exterior_14, w)
      .reorder(c_vec, c, w, n);
  bias1_im_0_d_def__
      .split(v458, v458, v458_vec, 8, TailStrategy::RoundUp).vectorize(v458_vec)
      .unroll(v458)
      .compute_root()
      .serial(v458)
      .reorder(v458_vec, v458);
  bias1_im_0_d_def__.update(0)
      .split(v458, v458, v458_vec, 8, TailStrategy::RoundUp).vectorize(v458_vec)
      .unroll(v458)
      .reorder(v458_vec, v458, r1081_x);
  constant_exterior_20
      .split(v537, v537, v537_vec, 8, TailStrategy::RoundUp).vectorize(v537_vec)
      .unroll(v537)
      .compute_root()
      .serial(v537)
      .reorder(v537_vec, v537);
  sum
      .split(n, n, n_vec, 8, TailStrategy::RoundUp).vectorize(n_vec)
      .unroll(n)
      .compute_root()
      .serial(n)
      .reorder(n_vec, n);
  sum.update(0)
      .split(n, n, n_vec, 8, TailStrategy::RoundUp).vectorize(n_vec)
      .unroll(n)
      .reorder(n_vec, n, r34_x, r34_y);
  constant_exterior_19
      .store_in(MemoryType::Stack)
      .split(v541, v541, v541_vec, 8, TailStrategy::RoundUp).vectorize(v541_vec)
      .unroll(v541)
      .unroll(v542)
      .compute_at(updated_head2_filter, v2897i)
      .store_at(updated_head2_filter, v2896)
      .reorder(v541_vec, v541, v542);
  loss_output
      .compute_root();
  constant_exterior_34
      .split(v458, v458, v458_vec, 8, TailStrategy::RoundUp).vectorize(v458_vec)
      .unroll(v458)
      .compute_root()
      .serial(v458)
      .reorder(v458_vec, v458);
  repeat_edge_14
      .store_in(MemoryType::Stack)
      .split(c, c, c_vec, 8, TailStrategy::RoundUp).vectorize(c_vec)
      .compute_at(constant_exterior_14, ni)
      .store_at(constant_exterior_14, w)
      .reorder(c_vec, c, w, n);
  constant_exterior_13
      .store_in(MemoryType::Stack)
      .split(c, c, c_vec, 8, TailStrategy::RoundUp).vectorize(c_vec)
      .compute_at(constant_exterior_14, ni)
      .store_at(constant_exterior_14, w)
      .reorder(c_vec, c, w, n);
  repeat_edge_13
      .store_in(MemoryType::Stack)
      .split(c, c, c_vec, 8, TailStrategy::RoundUp).vectorize(c_vec)
      .compute_at(constant_exterior_14, ni)
      .store_at(constant_exterior_14, w)
      .reorder(c_vec, c, w, n);
  repeat_edge_19
      .store_in(MemoryType::Stack)
      .split(v541, v541, v541_vec, 8, TailStrategy::RoundUp).vectorize(v541_vec)
      .unroll(v541)
      .unroll(v542)
      .compute_at(updated_head2_filter, v2897i)
      .store_at(updated_head2_filter, v2896)
      .reorder(v541_vec, v541, v542);
  constant_exterior_12
      .store_in(MemoryType::Stack)
      .split(c, c, c_vec, 8, TailStrategy::RoundUp).vectorize(c_vec)
      .compute_at(constant_exterior_14, ni)
      .store_at(constant_exterior_14, w)
      .reorder(c_vec, c, w, n);
  sum_1
      .compute_root();
  sum_1.update(0)
      .reorder(r34_x, r34_y);
  filter1_im_0_d_def__
      .split(v522, v522, v522_vec, 8, TailStrategy::RoundUp).vectorize(v522_vec)
      .compute_root()
      .serial(v524)
      .serial(v523)
      .reorder(v522_vec, v522, v523, v524);
  filter1_im_0_d_def__.update(0)
      .split(v522, v522, v522_vec, 8, TailStrategy::RoundUp).vectorize(v522_vec)
      .serial(r743_w)
      .serial(r743_z)
      .reorder(v522_vec, v522, r743_x, r743_y, r743_z, r743_w);
  filter1_im_0_d_def__.update(1)
      .split(v522, v522, v522_vec, 8, TailStrategy::RoundUp).vectorize(v522_vec)
      .serial(r1062_z)
      .serial(r1062_y)
      .reorder(v522_vec, v522, r1062_x, r1062_y, r1062_z);
  updated_bias1
      .split(v2950, v2950, v2950_vec, 8, TailStrategy::ShiftInwards).vectorize(v2950_vec)
      .unroll(v2950)
      .unroll(v2951)
      .compute_root()
      .serial(v2951)
      .serial(v2950)
      .reorder(v2950_vec, v2950, v2951);
  updated_bias1.update(0)
      .split(v2950, v2950, v2950_vec, 8, TailStrategy::GuardWithIf).vectorize(v2950_vec)
      .unroll(v2950)
      .serial(v2950)
      .reorder(v2950_vec, v2950);
  updated_bias1.update(1)
      .split(v2950, v2950, v2950_vec, 8, TailStrategy::GuardWithIf).vectorize(v2950_vec)
      .unroll(v2950)
      .serial(v2950)
      .reorder(v2950_vec, v2950);
  updated_bias1.update(2)
      .split(v2950, v2950, v2950_vec, 8, TailStrategy::GuardWithIf).vectorize(v2950_vec)
      .unroll(v2950)
      .serial(v2950)
      .reorder(v2950_vec, v2950);
  updated_bias1.update(3)
      .split(v2950, v2950, v2950_vec, 8, TailStrategy::GuardWithIf).vectorize(v2950_vec)
      .unroll(v2950)
      .serial(v2950)
      .reorder(v2950_vec, v2950);
  head2_filter_im_0_d_def__
      .split(v541, v541, v541_vec, 8, TailStrategy::RoundUp).vectorize(v541_vec)
      .compute_at(updated_head2_filter, v2896)
      .reorder(v541_vec, v541, v542);
  head2_filter_im_0_d_def__.update(0)
      .split(v541, v541, v541_vec, 8, TailStrategy::RoundUp).vectorize(v541_vec)
      .reorder(v541_vec, v541, r856_x, r856_y, r856_z);
  constant_exterior_11
      .store_in(MemoryType::Stack)
      .split(c, c, c_vec, 8, TailStrategy::RoundUp).vectorize(c_vec)
      .compute_at(constant_exterior_14, ni)
      .store_at(constant_exterior_14, w)
      .reorder(c_vec, c, w, n);
  updated_filter1
      .split(v2926, v2926, v2926_vec, 8, TailStrategy::ShiftInwards).vectorize(v2926_vec)
      .compute_root()
      .serial(v2929)
      .serial(v2928)
      .serial(v2927)
      .reorder(v2926_vec, v2926, v2927, v2928, v2929);
  updated_filter1.update(0)
      .split(v2927, v2927, v2927i, 3, TailStrategy::GuardWithIf)
      .split(v2926, v2926, v2926_vec, 8, TailStrategy::GuardWithIf).vectorize(v2926_vec)
      .unroll(v2926)
      .unroll(v2927i)
      .serial(v2927)
      .reorder(v2926_vec, v2926, v2927i, v2928, v2927);
  updated_filter1.update(1)
      .split(v2926, v2926, v2926_vec, 8, TailStrategy::GuardWithIf).vectorize(v2926_vec)
      .serial(v2928)
      .serial(v2927)
      .reorder(v2926_vec, v2926, v2927, v2928);
  updated_filter1.update(2)
      .split(v2926, v2926, v2926_vec, 8, TailStrategy::GuardWithIf).vectorize(v2926_vec)
      .serial(v2928)
      .serial(v2927)
      .reorder(v2926_vec, v2926, v2927, v2928);
  updated_filter1.update(3)
      .split(v2926, v2926, v2926_vec, 8, TailStrategy::GuardWithIf).vectorize(v2926_vec)
      .serial(v2928)
      .serial(v2927)
      .reorder(v2926_vec, v2926, v2927, v2928);
  head2_bias_im_0_d_def__
      .split(v537, v537, v537_vec, 8, TailStrategy::RoundUp).vectorize(v537_vec)
      .unroll(v537)
      .compute_root()
      .serial(v537)
      .reorder(v537_vec, v537);
  head2_bias_im_0_d_def__.update(0)
      .split(v537, v537, v537_vec, 8, TailStrategy::RoundUp).vectorize(v537_vec)
      .unroll(v537)
      .reorder(v537_vec, v537, r875_x, r875_y);
  repeat_edge_11
      .store_in(MemoryType::Stack)
      .split(c, c, c_vec, 8, TailStrategy::RoundUp).vectorize(c_vec)
      .compute_at(constant_exterior_14, ni)
      .store_at(constant_exterior_14, w)
      .reorder(c_vec, c, w, n);
  updated_head2_bias
      .split(v2914, v2914, v2914_vec, 8, TailStrategy::ShiftInwards).vectorize(v2914_vec)
      .unroll(v2914)
      .unroll(v2915)
      .compute_root()
      .serial(v2915)
      .serial(v2914)
      .reorder(v2914_vec, v2914, v2915);
  updated_head2_bias.update(0)
      .split(v2914, v2914, v2914_vec, 8, TailStrategy::GuardWithIf).vectorize(v2914_vec)
      .unroll(v2914)
      .serial(v2914)
      .reorder(v2914_vec, v2914);
  updated_head2_bias.update(1)
      .split(v2914, v2914, v2914_vec, 8, TailStrategy::GuardWithIf).vectorize(v2914_vec)
      .unroll(v2914)
      .serial(v2914)
      .reorder(v2914_vec, v2914);
  updated_head2_bias.update(2)
      .split(v2914, v2914, v2914_vec, 8, TailStrategy::GuardWithIf).vectorize(v2914_vec)
      .unroll(v2914)
      .serial(v2914)
      .reorder(v2914_vec, v2914);
  updated_head2_bias.update(3)
      .split(v2914, v2914, v2914_vec, 8, TailStrategy::GuardWithIf).vectorize(v2914_vec)
      .unroll(v2914)
      .serial(v2914)
      .reorder(v2914_vec, v2914);
  repeat_edge_1_0_d_def__
      .store_in(MemoryType::Stack)
      .split(c, c, c_vec, 8, TailStrategy::RoundUp).vectorize(c_vec)
      .compute_at(constant_exterior_14, ni)
      .store_at(constant_exterior_14, w)
      .reorder(c_vec, c, w, n);
  updated_head2_filter
      .split(v2896, v2896, v2896_vec, 8, TailStrategy::ShiftInwards).vectorize(v2896_vec)
      .compute_root()
      .serial(v2898)
      .serial(v2897)
      .serial(v2896)
      .reorder(v2896_vec, v2896, v2897, v2898);
  updated_head2_filter.update(0)
      .split(v2896, v2896, v2896i, 8, TailStrategy::GuardWithIf)
      .split(v2897, v2897, v2897i, 3, TailStrategy::GuardWithIf)
      .split(v2896i, v2896i, v2896i_vec, 8, TailStrategy::GuardWithIf).vectorize(v2896i_vec)
      .unroll(v2896i)
      .serial(v2897)
      .serial(v2896)
      .reorder(v2896i_vec, v2896i, v2897i, v2896, v2897);
  updated_head2_filter.update(1)
      .split(v2896, v2896, v2896_vec, 8, TailStrategy::GuardWithIf).vectorize(v2896_vec)
      .serial(v2897)
      .serial(v2896)
      .reorder(v2896_vec, v2896, v2897);
  updated_head2_filter.update(2)
      .split(v2896, v2896, v2896_vec, 8, TailStrategy::GuardWithIf).vectorize(v2896_vec)
      .serial(v2897)
      .serial(v2896)
      .reorder(v2896_vec, v2896, v2897);
  updated_head2_filter.update(3)
      .split(v2896, v2896, v2896_vec, 8, TailStrategy::GuardWithIf).vectorize(v2896_vec)
      .serial(v2897)
      .serial(v2896)
      .reorder(v2896_vec, v2896, v2897);
  head2_relu_0_d_def__
      .store_in(MemoryType::Stack)
      .split(c, c, c_vec, 8, TailStrategy::RoundUp).vectorize(c_vec)
      .compute_at(constant_exterior_14, ni)
      .reorder(c_vec, c, w, n);
  head2_relu_0_d_def__.update(0)
      .split(c, c, c_vec, 8, TailStrategy::RoundUp).vectorize(c_vec)
      .unroll(c)
      .unroll(n)
      .reorder(c_vec, c, n, r790_x);
  constant_exterior_33
      .split(v461, v461, v461_vec, 8, TailStrategy::RoundUp).vectorize(v461_vec)
      .unroll(v461)
      .compute_root()
      .serial(v461)
      .reorder(v461_vec, v461);
  constant_exterior_1_0_d_def__
      .split(c, c, c_vec, 8, TailStrategy::RoundUp).vectorize(c_vec)
      .compute_at(constant_exterior_14, w)
      .reorder(c_vec, c, w, n);
  constant_exterior_1_0_d_def__.update(0)
      .reorder(r714_x, r714_y, r714_z, w, n);
  head1_bias_im_0_d_def__
      .split(v461, v461, v461_vec, 8, TailStrategy::RoundUp).vectorize(v461_vec)
      .unroll(v461)
      .compute_root()
      .serial(v461)
      .reorder(v461_vec, v461);
  head1_bias_im_0_d_def__.update(0)
      .split(v461, v461, v461_vec, 8, TailStrategy::RoundUp).vectorize(v461_vec)
      .unroll(v461)
      .reorder(v461_vec, v461, r1213_x);
  constant_exterior_32
      .store_in(MemoryType::Stack)
      .split(v466, v466, v466_vec, 8, TailStrategy::RoundUp).vectorize(v466_vec)
      .unroll(v466)
      .unroll(v467)
      .unroll(v468)
      .compute_at(updated_head1_filter, v2862i)
      .store_at(updated_head1_filter, v2860)
      .reorder(v466_vec, v466, v467, v468);
  updated_head1_bias
      .split(v2884, v2884, v2884_vec, 8, TailStrategy::ShiftInwards).vectorize(v2884_vec)
      .unroll(v2884)
      .unroll(v2885)
      .compute_root()
      .serial(v2885)
      .serial(v2884)
      .reorder(v2884_vec, v2884, v2885);
  updated_head1_bias.update(0)
      .split(v2884, v2884, v2884_vec, 8, TailStrategy::GuardWithIf).vectorize(v2884_vec)
      .unroll(v2884)
      .serial(v2884)
      .reorder(v2884_vec, v2884);
  updated_head1_bias.update(1)
      .split(v2884, v2884, v2884_vec, 8, TailStrategy::GuardWithIf).vectorize(v2884_vec)
      .unroll(v2884)
      .serial(v2884)
      .reorder(v2884_vec, v2884);
  updated_head1_bias.update(2)
      .split(v2884, v2884, v2884_vec, 8, TailStrategy::GuardWithIf).vectorize(v2884_vec)
      .unroll(v2884)
      .serial(v2884)
      .reorder(v2884_vec, v2884);
  updated_head1_bias.update(3)
      .split(v2884, v2884, v2884_vec, 8, TailStrategy::GuardWithIf).vectorize(v2884_vec)
      .unroll(v2884)
      .serial(v2884)
      .reorder(v2884_vec, v2884);
  repeat_edge_32
      .split(v466, v466, v466_vec, 8, TailStrategy::RoundUp).vectorize(v466_vec)
      .compute_at(updated_head1_filter, v2860)
      .reorder(v466_vec, v466, v467, v468);
  constant_exterior_27
      .split(c, c, ci, 8, TailStrategy::RoundUp)
      .split(w, w, wi, 2, TailStrategy::RoundUp)
      .split(ci, ci, ci_vec, 8, TailStrategy::RoundUp).vectorize(ci_vec)
      .unroll(ci)
      .unroll(wi)
      .compute_root()
      .serial(w)
      .serial(c)
      .reorder(ci_vec, ci, wi, c, w);
  head1_filter_im_0_d_def__
      .split(v466, v466, v466_vec, 8, TailStrategy::RoundUp).vectorize(v466_vec)
      .compute_root()
      .serial(v468)
      .serial(v467)
      .reorder(v466_vec, v466, v467, v468);
  head1_filter_im_0_d_def__.update(0)
      .split(v466, v466, v466_vec, 8, TailStrategy::RoundUp).vectorize(v466_vec)
      .serial(r1194_z)
      .serial(r1194_y)
      .reorder(v466_vec, v466, r1194_x, r1194_y, r1194_z);
  repeat_edge_27
      .split(c, c, c_vec, 8, TailStrategy::RoundUp).vectorize(c_vec)
      .unroll(c)
      .unroll(w)
      .compute_at(constant_exterior_27, c)
      .reorder(c_vec, c, w);
  updated_head1_filter
      .split(v2860, v2860, v2860_vec, 8, TailStrategy::ShiftInwards).vectorize(v2860_vec)
      .compute_root()
      .serial(v2863)
      .serial(v2862)
      .serial(v2861)
      .reorder(v2860_vec, v2860, v2861, v2862, v2863);
  updated_head1_filter.update(0)
      .split(v2860, v2860, v2860i, 8, TailStrategy::GuardWithIf)
      .split(v2861, v2861, v2861i, 14, TailStrategy::GuardWithIf)
      .split(v2862, v2862, v2862i, 4, TailStrategy::GuardWithIf)
      .split(v2860i, v2860i, v2860i_vec, 8, TailStrategy::GuardWithIf).vectorize(v2860i_vec)
      .unroll(v2860i)
      .unroll(v2861i)
      .serial(v2862)
      .serial(v2861)
      .serial(v2860)
      .reorder(v2860i_vec, v2860i, v2861i, v2862i, v2860, v2861, v2862);
  updated_head1_filter.update(1)
      .split(v2860, v2860, v2860_vec, 8, TailStrategy::GuardWithIf).vectorize(v2860_vec)
      .serial(v2862)
      .serial(v2861)
      .reorder(v2860_vec, v2860, v2861, v2862);
  updated_head1_filter.update(2)
      .split(v2860, v2860, v2860_vec, 8, TailStrategy::GuardWithIf).vectorize(v2860_vec)
      .serial(v2862)
      .serial(v2861)
      .reorder(v2860_vec, v2860, v2861, v2862);
  updated_head1_filter.update(3)
      .split(v2860, v2860, v2860_vec, 8, TailStrategy::GuardWithIf).vectorize(v2860_vec)
      .serial(v2862)
      .serial(v2861)
      .reorder(v2860_vec, v2860, v2861, v2862);
  head2_conv
      .split(c, c, c_vec, 8, TailStrategy::RoundUp).vectorize(c_vec)
      .compute_root()
      .serial(n)
      .serial(w)
      .reorder(c_vec, c, w, n);
  head2_conv.update(0)
      .split(c, c, c_vec, 8, TailStrategy::RoundUp).vectorize(c_vec)
      .serial(n)
      .serial(w)
      .reorder(c_vec, c, r9_x, w, n);
  head1_relu_0_d_def__
      .split(c, c, c_vec, 8, TailStrategy::RoundUp).vectorize(c_vec)
      .compute_root()
      .serial(w)
      .serial(c)
      .reorder(c_vec, c, w);
  head1_relu_0_d_def__.update(0)
      .split(c, c, c_vec, 8, TailStrategy::RoundUp).vectorize(c_vec)
      .unroll(c)
      .reorder(c_vec, c, r1118_x);
  constant_exterior_22
      .split(c, c, c_vec, 8, TailStrategy::RoundUp).vectorize(c_vec)
      .unroll(c)
      .compute_root()
      .serial(w)
      .reorder(c_vec, c, w);
  constant_exterior_0_d_def__
      .split(c, c, c_vec, 8, TailStrategy::RoundUp).vectorize(c_vec)
      .compute_root()
      .serial(w)
      .serial(c)
      .reorder(c_vec, c, w);
  constant_exterior_0_d_def__.update(0)
      .serial(w)
      .reorder(r1033_x, r1033_y, r1033_z, w);
  repeat_edge_22
      .split(c, c, c_vec, 8, TailStrategy::RoundUp).vectorize(c_vec)
      .unroll(c)
      .unroll(w)
      .compute_at(constant_exterior_22, w)
      .reorder(c_vec, c, w);
  constant_exterior_10
      .split(w, w, wi, 9, TailStrategy::RoundUp)
      .split(n, n, ni, 8, TailStrategy::RoundUp)
      .split(c, c, c_vec, 8, TailStrategy::RoundUp).vectorize(c_vec)
      .compute_root()
      .serial(n)
      .serial(w)
      .reorder(c_vec, c, wi, ni, w, n);
  repeat_edge_10
      .store_in(MemoryType::Stack)
      .split(c, c, c_vec, 8, TailStrategy::RoundUp).vectorize(c_vec)
      .compute_at(constant_exterior_10, ni)
      .store_at(constant_exterior_10, w)
      .reorder(c_vec, c, w, n);
  conv1_stage1_1_d_def__
      .split(c, c, c_vec, 8, TailStrategy::RoundUp).vectorize(c_vec)
      .compute_root()
      .serial(w)
      .serial(c)
      .reorder(c_vec, c, w);
  conv1_stage1_1_d_def__.update(0)
      .split(c, c, c_vec, 8, TailStrategy::RoundUp).vectorize(c_vec)
      .serial(w)
      .reorder(c_vec, c, r762_x, w);
  conv1_stage1_1_d_def__.update(1)
      .split(r1000_x, r1000_x, r1000_xi, 6, TailStrategy::RoundUp)
      .split(r1000_y, r1000_y, r1000_yi, 2, TailStrategy::RoundUp)
      .split(r1000_xi, r1000_xi, r1000_xii, 3, TailStrategy::GuardWithIf)
      .unroll(r1000_xii)
      .serial(r1000_y)
      .serial(r1000_x)
      .reorder(r1000_xii, r1000_xi, r1000_yi, r1000_x, r1000_y);
  constant_exterior_9
      .store_in(MemoryType::Stack)
      .split(c, c, c_vec, 8, TailStrategy::RoundUp).vectorize(c_vec)
      .compute_at(conv1_stage2_1_d_def__, ni)
      .store_at(conv1_stage2_1_d_def__, n)
      .reorder(c_vec, c, w, n);
  repeat_edge_9
      .store_in(MemoryType::Stack)
      .split(c, c, c_vec, 8, TailStrategy::RoundUp).vectorize(c_vec)
      .compute_at(conv1_stage2_1_d_def__, n)
      .store_at(constant_exterior_10, w)
      .reorder(c_vec, c, w, n);
  conv1_stage2_1_d_def__
      .split(c, c, c_vec, 8, TailStrategy::RoundUp).vectorize(c_vec)
      .compute_at(constant_exterior_10, w)
      .reorder(c_vec, c, w, n);
  conv1_stage2_1_d_def__.update(0)
      .split(r210_x, r210_x, r210_xi, 4, TailStrategy::RoundUp)
      .split(n, n, ni, 4, TailStrategy::RoundUp)
      .reorder(r210_xi, r210_y, ni, r210_x, n);
  conv1_stage2_1_d_def__.update(1)
      .split(n, n, ni, 4, TailStrategy::RoundUp)
      .split(c, c, c_vec, 8, TailStrategy::RoundUp).vectorize(c_vec)
      .reorder(c_vec, c, w, ni, n);
  constant_exterior_8
      .split(n, n, n_vec, 8, TailStrategy::RoundUp).vectorize(n_vec)
      .unroll(n)
      .unroll(w)
      .compute_at(constant_exterior_10, w)
      .reorder(n_vec, n, w);
  repeat_edge_8
      .split(n, n, ni, 8, TailStrategy::RoundUp)
      .split(w, w, wi, 7, TailStrategy::RoundUp)
      .split(ni, ni, ni_vec, 8, TailStrategy::RoundUp).vectorize(ni_vec)
      .unroll(ni)
      .unroll(wi)
      .compute_root()
      .serial(w)
      .serial(n)
      .reorder(ni_vec, ni, wi, n, w);
  f0_0_d_def__
      .split(n, n, n_vec, 8, TailStrategy::RoundUp).vectorize(n_vec)
      .unroll(n)
      .unroll(w)
      .compute_at(repeat_edge_8, n)
      .reorder(n_vec, n, w);
  f0_0_d_def__.update(0)
      .split(n, n, n_vec, 8, TailStrategy::RoundUp).vectorize(n_vec)
      .unroll(n)
      .unroll(r238_x)
      .reorder(n_vec, n, r238_x);
  constant_exterior_7
      .split(n, n, n_vec, 8, TailStrategy::RoundUp).vectorize(n_vec)
      .unroll(n)
      .compute_root()
      .serial(n)
      .reorder(n_vec, n);
  repeat_edge_7
      .split(n, n, n_vec, 8, TailStrategy::RoundUp).vectorize(n_vec)
      .unroll(n)
      .compute_root()
      .serial(n)
      .reorder(n_vec, n);
  true_runtime_im
      .split(_0, _0, _0_vec, 8, TailStrategy::ShiftInwards).vectorize(_0_vec)
      .unroll(_0)
      .compute_root()
      .serial(_0)
      .reorder(_0_vec, _0);
  f1
      .split(n, n, n_vec, 8, TailStrategy::RoundUp).vectorize(n_vec)
      .unroll(n)
      .compute_root()
      .serial(n)
      .reorder(n_vec, n);
  f1.update(0)
      .split(n, n, n_vec, 8, TailStrategy::RoundUp).vectorize(n_vec)
      .unroll(n)
      .reorder(n_vec, n, r24_x);
  constant_exterior_1
      .split(w, w, wi, 8, TailStrategy::RoundUp)
      .split(n, n, ni, 8, TailStrategy::RoundUp)
      .split(c, c, c_vec, 8, TailStrategy::RoundUp).vectorize(c_vec)
      .compute_root()
      .serial(n)
      .serial(w)
      .reorder(c_vec, c, wi, ni, w, n);
  relu1_0_d_def__
      .split(c, c, c_vec, 8, TailStrategy::RoundUp).vectorize(c_vec)
      .compute_at(constant_exterior_10, w)
      .reorder(c_vec, c, w, n);
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
  conv1_stage2
      .split(c, c, c_vec, 8, TailStrategy::RoundUp).vectorize(c_vec)
      .compute_root()
      .serial(n)
      .serial(w)
      .reorder(c_vec, c, w, n);
  conv1_stage2.update(0)
      .split(c, c, c_vec, 8, TailStrategy::RoundUp).vectorize(c_vec)
      .serial(n)
      .serial(w)
      .reorder(c_vec, c, r19_x, r19_y, w, n);
  repeat_edge_1
      .store_in(MemoryType::Stack)
      .split(c, c, c_vec, 8, TailStrategy::RoundUp).vectorize(c_vec)
      .compute_at(constant_exterior_1, ni)
      .store_at(constant_exterior_1, w)
      .reorder(c_vec, c, w, n);
  head2_relu
      .store_in(MemoryType::Stack)
      .split(c, c, c_vec, 8, TailStrategy::RoundUp).vectorize(c_vec)
      .compute_at(constant_exterior_1, ni)
      .store_at(constant_exterior_1, w)
      .reorder(c_vec, c, w, n);
  normalized_schedule_features
      .split(c, c, ci, 7, TailStrategy::ShiftInwards)
      .split(s, s, si, 4, TailStrategy::ShiftInwards)
      .split(ci, ci, cii, 4, TailStrategy::ShiftInwards)
      .split(si, si, sii, 2, TailStrategy::ShiftInwards)
      .split(n, n, n_vec, 8, TailStrategy::ShiftInwards).vectorize(n_vec)
      .compute_root()
      .serial(s)
      .serial(c)
      .reorder(n_vec, n, cii, sii, ci, si, c, s);
  schedule_std_im
      .store_in(MemoryType::Stack)
      .split(_0, _0, _0_vec, 4, TailStrategy::ShiftInwards).vectorize(_0_vec)
      .unroll(_0)
      .compute_at(normalized_schedule_features, sii)
      .store_at(normalized_schedule_features, ci)
      .reorder(_0_vec, _0);
  schedule_mean_im
      .split(_0, _0, _0_vec, 4, TailStrategy::ShiftInwards).vectorize(_0_vec)
      .unroll(_0)
      .compute_at(normalized_schedule_features, c)
      .reorder(_0_vec, _0);
  schedule_features_im
      .split(_0, _0, _0_vec, 8, TailStrategy::ShiftInwards).vectorize(_0_vec)
      .compute_root()
      .serial(_2)
      .serial(_1)
      .reorder(_0_vec, _0, _1, _2);
  head2_filter_im
      .split(_0, _0, _0_vec, 8, TailStrategy::ShiftInwards).vectorize(_0_vec)
      .compute_root()
      .serial(_1)
      .serial(_0)
      .reorder(_0_vec, _0, _1);
  head2_bias_im
      .split(_0, _0, _0_vec, 8, TailStrategy::ShiftInwards).vectorize(_0_vec)
      .unroll(_0)
      .compute_root()
      .serial(_0)
      .reorder(_0_vec, _0);
  constant_exterior_6
      .store_in(MemoryType::Stack)
      .unroll(n)
      .compute_at(conv1_stage2_1_d_def__, ni)
      .store_at(conv1_stage2_1_d_def__, r210_x);
  repeat_edge_6
      .split(n, n, n_vec, 8, TailStrategy::RoundUp).vectorize(n_vec)
      .unroll(n)
      .compute_root()
      .serial(n)
      .reorder(n_vec, n);
  repeat_edge_4
      .split(n, n, n_vec, 8, TailStrategy::RoundUp).vectorize(n_vec)
      .unroll(n)
      .compute_root()
      .serial(n)
      .reorder(n_vec, n);
  constant_exterior_3
      .compute_at(f2_0_d_def__, r117_x);
  f2_0_d_def__
      .split(n, n, n_vec, 8, TailStrategy::RoundUp).vectorize(n_vec)
      .unroll(n)
      .compute_root()
      .serial(n)
      .reorder(n_vec, n);
  f2_0_d_def__.update(0)
      .split(r117_x, r117_x, r117_xi, 2, TailStrategy::RoundUp)
      .unroll(r117_xi)
      .serial(r117_x)
      .reorder(r117_xi, r117_x);
  repeat_edge_3
      .compute_at(f2_0_d_def__, r117_x);
  sum_2_1_d_def__
      .store_in(MemoryType::Stack)
      .compute_at(f2_0_d_def__, r117_x);
  filter1_im
      .split(_0, _0, _0_vec, 8, TailStrategy::ShiftInwards).vectorize(_0_vec)
      .compute_root()
      .serial(_2)
      .serial(_1)
      .reorder(_0_vec, _0, _1, _2);
  conv1_stage1
      .split(c, c, c_vec, 8, TailStrategy::RoundUp).vectorize(c_vec)
      .compute_root()
      .serial(w)
      .serial(c)
      .reorder(c_vec, c, w);
  conv1_stage1.update(0)
      .split(c, c, c_vec, 8, TailStrategy::RoundUp).vectorize(c_vec)
      .serial(w)
      .reorder(c_vec, c, r14_x, r14_y, w);
  constant_exterior
      .split(c, c, ci, 8, TailStrategy::RoundUp)
      .split(ci, ci, ci_vec, 8, TailStrategy::RoundUp).vectorize(ci_vec)
      .unroll(ci)
      .compute_root()
      .serial(w)
      .serial(c)
      .reorder(ci_vec, ci, c, w);
  repeat_edge
      .split(c, c, ci, 2, TailStrategy::RoundUp)
      .unroll(ci)
      .compute_at(constant_exterior, c)
      .reorder(ci, c, w);
  head1_relu
      .store_in(MemoryType::Stack)
      .unroll(c)
      .unroll(w)
      .compute_at(repeat_edge, c)
      .reorder(c, w);
  normalized_pipeline_features
      .split(j, j, ji, 2, TailStrategy::ShiftInwards)
      .split(s, s, si, 4, TailStrategy::ShiftInwards)
      .split(c, c, ci, 14, TailStrategy::ShiftInwards)
      .split(si, si, sii, 2, TailStrategy::ShiftInwards)
      .split(ci, ci, cii, 7, TailStrategy::ShiftInwards)
      .split(cii, cii, cii_vec, 4, TailStrategy::ShiftInwards).vectorize(cii_vec)
      .unroll(cii)
      .unroll(ji)
      .compute_root()
      .serial(s)
      .serial(j)
      .reorder(cii_vec, cii, ji, sii, ci, c, si, j, s);
  head1_conv
      .split(c, c, c_vec, 8, TailStrategy::RoundUp).vectorize(c_vec)
      .compute_root()
      .serial(w)
      .serial(c)
      .reorder(c_vec, c, w);
  head1_conv.update(0)
      .split(c, c, c_vec, 8, TailStrategy::RoundUp).vectorize(c_vec)
      .serial(w)
      .reorder(c_vec, c, r4_x, r4_y, w);
  pipeline_std_im
      .store_in(MemoryType::Stack)
      .split(_0, _0, _0_vec, 8, TailStrategy::ShiftInwards).vectorize(_0_vec)
      .unroll(_0)
      .unroll(_1)
      .compute_at(normalized_pipeline_features, c)
      .reorder(_0_vec, _0, _1);
  pipeline_mean_im
      .split(_0, _0, _0_vec, 8, TailStrategy::ShiftInwards).vectorize(_0_vec)
      .unroll(_0)
      .unroll(_1)
      .compute_at(normalized_pipeline_features, j)
      .reorder(_0_vec, _0, _1);
  pipeline_features_im
      .store_in(MemoryType::Stack)
      .split(_0, _0, _0_vec, 4, TailStrategy::ShiftInwards).vectorize(_0_vec)
      .unroll(_0)
      .unroll(_1)
      .unroll(_2)
      .compute_at(normalized_pipeline_features, sii)
      .store_at(normalized_pipeline_features, ci)
      .reorder(_0_vec, _0, _1, _2);
  head1_filter_im
      .split(_0, _0, _0_vec, 8, TailStrategy::ShiftInwards).vectorize(_0_vec)
      .compute_root()
      .serial(_2)
      .serial(_1)
      .reorder(_0_vec, _0, _1, _2);
  head1_bias_im
      .split(_0, _0, _0_vec, 8, TailStrategy::ShiftInwards).vectorize(_0_vec)
      .unroll(_0)
      .compute_root()
      .serial(_0)
      .reorder(_0_vec, _0);
  bias1_im
      .split(_0, _0, _0_vec, 8, TailStrategy::ShiftInwards).vectorize(_0_vec)
      .unroll(_0)
      .compute_root()
      .serial(_0)
      .reorder(_0_vec, _0);
  constant_exterior_21
      .store_in(MemoryType::Stack)
      .compute_at(conv1_stage1_1_d_def__, r1000_xi);
  repeat_edge_21
      .store_in(MemoryType::Stack)
      .compute_at(conv1_stage1_1_d_def__, r1000_xi);
  sum_1_1_d_def__
      .store_in(MemoryType::Stack)
      .compute_at(conv1_stage1_1_d_def__, r1000_yi)
      .store_at(conv1_stage1_1_d_def__, r1000_x);
  constant_exterior_2
      .compute_root();
  repeat_edge_2
      .compute_root();
  loss_output_0_d_def__
      .compute_root();
  adjoint
      .compute_root();
}
