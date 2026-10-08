project('libmpeghe', 'c', version: '1.0.0')

cc = meson.get_compiler('c')

libmpeghe_c_args = []

cpu_family = host_machine.cpu_family()
if cpu_family == 'x86' or cpu_family == 'x86_64'
  libmpeghe_c_args += ['-D_X86_']
elif cpu_family == 'arm'
  libmpeghe_c_args += ['-D_ARM_']
elif cpu_family == 'aarch64'
  libmpeghe_c_args += ['-D_ARM_']
endif

# The encoder relies on -O3 for performance and gnu99 extensions on ARM.
if cc.has_argument('-fno-strict-aliasing')
  libmpeghe_c_args += ['-fno-strict-aliasing']
endif

math_dep = cc.find_library('m', required: false)

encoder_sources = files(
  'encoder/impeghe_acelp_enc.c',
  'encoder/impeghe_acelp_rom.c',
  'encoder/impeghe_acelp_tools.c',
  'encoder/impeghe_api.c',
  'encoder/impeghe_arith_enc.c',
  'encoder/impeghe_asc_write.c',
  'encoder/impeghe_avq_enc.c',
  'encoder/impeghe_avq_rom.c',
  'encoder/impeghe_bitbuffer.c',
  'encoder/impeghe_block_switch.c',
  'encoder/impeghe_cplx_pred.c',
  'encoder/impeghe_crc.c',
  'encoder/impeghe_dmx_cicp2geometry.c',
  'encoder/impeghe_dmx_matrix_common.c',
  'encoder/impeghe_dmx_matrix_enc.c',
  'encoder/impeghe_dmx_matrix_enc_api.c',
  'encoder/impeghe_drc_api.c',
  'encoder/impeghe_drc_enc.c',
  'encoder/impeghe_drc_gain_calculator.c',
  'encoder/impeghe_drc_gain_enc.c',
  'encoder/impeghe_drc_mux.c',
  'encoder/impeghe_drc_tables.c',
  'encoder/impeghe_drc_uni_drc_eq.c',
  'encoder/impeghe_drc_uni_drc_filter_bank.c',
  'encoder/impeghe_enc_fac.c',
  'encoder/impeghe_enc_main.c',
  'encoder/impeghe_enc_mct.c',
  'encoder/impeghe_enc_mct_rom.c',
  'encoder/impeghe_fdp_enc.c',
  'encoder/impeghe_fd_enc.c',
  'encoder/impeghe_fd_fac.c',
  'encoder/impeghe_fd_mdct.c',
  'encoder/impeghe_fd_qc_adjthr.c',
  'encoder/impeghe_fd_qc_util.c',
  'encoder/impeghe_fd_quant.c',
  'encoder/impeghe_fft.c',
  'encoder/impeghe_hoa_amb_comp_mod.c',
  'encoder/impeghe_hoa_ch_assignment.c',
  'encoder/impeghe_hoa_common_functions.c',
  'encoder/impeghe_hoa_config.c',
  'encoder/impeghe_hoa_decomposition.c',
  'encoder/impeghe_hoa_dir_based_pre_dom_sound_syn.c',
  'encoder/impeghe_hoa_dir_vec_estimation.c',
  'encoder/impeghe_hoa_dyn_correction.c',
  'encoder/impeghe_hoa_encoder.c',
  'encoder/impeghe_hoa_fft.c',
  'encoder/impeghe_hoa_frame.c',
  'encoder/impeghe_hoa_init.c',
  'encoder/impeghe_hoa_matrix.c',
  'encoder/impeghe_hoa_pre_dom_sound_syn.c',
  'encoder/impeghe_hoa_rom.c',
  'encoder/impeghe_hoa_spatial_encoder.c',
  'encoder/impeghe_hoa_vector_based_predom_sound_syn.c',
  'encoder/impeghe_huffman_rom.c',
  'encoder/impeghe_igf_enc.c',
  'encoder/impeghe_lpc.c',
  'encoder/impeghe_lpc_avq.c',
  'encoder/impeghe_lpd_enc.c',
  'encoder/impeghe_lpd_rom.c',
  'encoder/impeghe_lpd_utils.c',
  'encoder/impeghe_ltpf_enc.c',
  'encoder/impeghe_ltpf_rom.c',
  'encoder/impeghe_mae_write.c',
  'encoder/impeghe_mhas_write.c',
  'encoder/impeghe_ms.c',
  'encoder/impeghe_nf.c',
  'encoder/impeghe_oam_enc.c',
  'encoder/impeghe_oam_enc_ld.c',
  'encoder/impeghe_oam_enc_utils.c',
  'encoder/impeghe_psy_mod.c',
  'encoder/impeghe_psy_rom.c',
  'encoder/impeghe_psy_utils.c',
  'encoder/impeghe_resampler.c',
  'encoder/impeghe_resampler_rom.c',
  'encoder/impeghe_rom.c',
  'encoder/impeghe_signal_classifier.c',
  'encoder/impeghe_signal_classifier_rom.c',
  'encoder/impeghe_simple_matrix.c',
  'encoder/impeghe_stereo_lpd.c',
  'encoder/impeghe_stereo_lpd_rom.c',
  'encoder/impeghe_tbe_enc.c',
  'encoder/impeghe_tbe_rom.c',
  'encoder/impeghe_tcx_enc.c',
  'encoder/impeghe_tcx_mdct.c',
  'encoder/impeghe_tns_usac.c',
  'encoder/impeghe_windowing.c',
  'encoder/impeghe_write_bitstream.c',
)

libmpeghe_include_dirs = include_directories('encoder', 'test')

libmpeghe_library = static_library(
  'mpeghe',
  encoder_sources,
  include_directories: libmpeghe_include_dirs,
  c_args: libmpeghe_c_args,
  dependencies: math_dep,
  build_by_default: false,
)

libmpeghe_dep = declare_dependency(
  link_with: libmpeghe_library,
  include_directories: libmpeghe_include_dirs,
)
