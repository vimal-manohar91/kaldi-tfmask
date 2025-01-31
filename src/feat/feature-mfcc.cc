// feat/feature-mfcc.cc

// Copyright 2009-2011  Karel Vesely;  Petr Motlicek
//           2014       Vimal Manohar

// See ../../COPYING for clarification regarding multiple authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//  http://www.apache.org/licenses/LICENSE-2.0
//
// THIS CODE IS PROVIDED *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED
// WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE,
// MERCHANTABLITY OR NON-INFRINGEMENT.
// See the Apache 2 License for the specific language governing permissions and
// limitations under the License.


#include "feat/feature-mfcc.h"
#include "feat/feature-fbank.h"

namespace kaldi {

Mfcc::Mfcc(const MfccOptions &opts)
    : opts_(opts), feature_window_function_(opts.frame_opts), srfft_(NULL) {
  int32 num_bins = opts.mel_opts.num_bins;
  Matrix<BaseFloat> dct_matrix(num_bins, num_bins);
  ComputeDctMatrix(&dct_matrix);
  // Note that we include zeroth dct in either case.  If using the
  // energy we replace this with the energy.  This means a different
  // ordering of features than HTK.
  SubMatrix<BaseFloat> dct_rows(dct_matrix, 0, opts.num_ceps, 0, num_bins);
  dct_matrix_.Resize(opts.num_ceps, num_bins);
  dct_matrix_.CopyFromMat(dct_rows);  // subset of rows.
  if (opts.cepstral_lifter != 0.0) {
    lifter_coeffs_.Resize(opts.num_ceps);
    ComputeLifterCoeffs(opts.cepstral_lifter, &lifter_coeffs_);
  }
  if (opts.energy_floor > 0.0)
    log_energy_floor_ = log(opts.energy_floor);

  int32 padded_window_size = opts.frame_opts.PaddedWindowSize();
  if ((padded_window_size & (padded_window_size-1)) == 0)  // Is a power of two...
    srfft_ = new SplitRadixRealFft<BaseFloat>(padded_window_size);
}

Mfcc::~Mfcc() {
  for (std::map<BaseFloat, MelBanks*>::iterator iter = mel_banks_.begin();
      iter != mel_banks_.end();
      ++iter)
    delete iter->second;
  if (srfft_ != NULL)
    delete srfft_;
}

const MelBanks *Mfcc::GetMelBanks(BaseFloat vtln_warp) {
  MelBanks *this_mel_banks = NULL;
  std::map<BaseFloat, MelBanks*>::iterator iter = mel_banks_.find(vtln_warp);
  if (iter == mel_banks_.end()) {
    this_mel_banks = new MelBanks(opts_.mel_opts,
                                  opts_.frame_opts,
                                  vtln_warp);
    mel_banks_[vtln_warp] = this_mel_banks;
  } else {
    this_mel_banks = iter->second;
  }
  return this_mel_banks;
}

void Mfcc::Compute(const VectorBase<BaseFloat> &wave,
                   BaseFloat vtln_warp,
                   Matrix<BaseFloat> *output,
                   Vector<BaseFloat> *wave_remainder) {
  KALDI_ASSERT(output != NULL);

  // Get dimensions of output features
  int32 rows_out = NumFrames(wave.Dim(), opts_.frame_opts),
      cols_out = opts_.num_ceps;
  if (rows_out == 0)
    KALDI_ERR << "No frames fit in file (#samples is " << wave.Dim() << ")";
  // Prepare the output buffer
  output->Resize(rows_out, cols_out);

  // Optionally extract the remainder for further processing
  if (wave_remainder != NULL)
    ExtractWaveformRemainder(wave, opts_.frame_opts, wave_remainder);

  // Buffers
  Vector<BaseFloat> window;  // windowed waveform.
  Vector<BaseFloat> mel_energies;

  // Compute all the freames, r is frame index..
  for (int32 r = 0; r < rows_out; r++) {  // r is frame index..
    // Cut the window, apply window function
    BaseFloat log_energy;
    ExtractWindow(wave, r, opts_.frame_opts, feature_window_function_, &window,
                  (opts_.use_energy && opts_.raw_energy ? &log_energy : NULL));

    // Compute energy after window function (not the raw one)
    if (opts_.use_energy && !opts_.raw_energy)
      log_energy = log(VecVec(window, window));

    if (srfft_ != NULL)  // Compute FFT using the split-radix algorithm.
      srfft_->Compute(window.Data(), true);
    else  // An alternative algorithm that works for non-powers-of-two.
      RealFft(&window, true);

    // Convert the FFT into a power spectrum.
    ComputePowerSpectrum(&window);
    SubVector<BaseFloat> power_spectrum(window, 0, window.Dim()/2 + 1);

    // Integrate with MelFiterbank over power spectrum
    const MelBanks *this_mel_banks = GetMelBanks(vtln_warp);
    this_mel_banks->Compute(power_spectrum, &mel_energies);

    mel_energies.ApplyLog();  // take the log.

    SubVector<BaseFloat> this_mfcc(output->Row(r));

    // this_mfcc = dct_matrix_ * mel_energies [which now have log]
    this_mfcc.AddMatVec(1.0, dct_matrix_, kNoTrans, mel_energies, 0.0);

    if (opts_.cepstral_lifter != 0.0)
      this_mfcc.MulElements(lifter_coeffs_);

    if (opts_.use_energy) {
      if (opts_.energy_floor > 0.0 && log_energy < log_energy_floor_)
        log_energy = log_energy_floor_;
      this_mfcc(0) = log_energy;
    }

    if (opts_.htk_compat) {
      BaseFloat energy = this_mfcc(0);
      for (int32 i = 0; i < opts_.num_ceps-1; i++)
        this_mfcc(i) = this_mfcc(i+1);
      if (!opts_.use_energy)
        energy *= M_SQRT2;  // scale on C0 (actually removing scale
      // we previously added that's part of one common definition of
      // cosine transform.)
      this_mfcc(opts_.num_ceps-1)  = energy;
    }
  }
}

void Mfcc::ComputeFromFbank(const Matrix<BaseFloat> &fbank,
                   Matrix<BaseFloat> *output) {
  KALDI_ASSERT(output != NULL);

  // Get dimensions of output features
  int32 rows_out = fbank.NumRows();
  int32 cols_out = opts_.num_ceps;

  if (!opts_.use_energy) {
    KALDI_ERR << "--use-energy is true in MFCC config. It has to be set false in order to compute MFCC from Fbank";
  }

  if (fbank.NumCols() != opts_.mel_opts.num_bins) {
    KALDI_ERR << "--num-mel-bins is " << opts_.mel_opts.num_bins << ". But fbank has " << fbank.NumCols() << "bins including energy";
  }

  if (rows_out == 0)
    KALDI_ERR << "No frames found in fbank";

  // Prepare the output buffer
  output->Resize(rows_out, cols_out);

  // Buffers
  Vector<BaseFloat> mel_energies(opts_.mel_opts.num_bins);

  // Compute all the freames, r is frame index..
  for (int32 r = 0; r < rows_out; r++) {  // r is frame index..
    // Cut the window, apply window function

    mel_energies.CopyFromVec(fbank.Row(r));

    SubVector<BaseFloat> this_mfcc(output->Row(r));

    // this_mfcc = dct_matrix_ * mel_energies [which now have log]
    this_mfcc.AddMatVec(1.0, dct_matrix_, kNoTrans, mel_energies, 0.0);

    if (opts_.cepstral_lifter != 0.0)
      this_mfcc.MulElements(lifter_coeffs_);

    if (opts_.htk_compat) {
      BaseFloat energy = this_mfcc(0);
      for (int32 i = 0; i < opts_.num_ceps-1; i++)
        this_mfcc(i) = this_mfcc(i+1);
      energy *= M_SQRT2;  // scale on C0 (actually removing scale
      // we previously added that's part of one common definition of
      // cosine transform.)
      this_mfcc(opts_.num_ceps-1)  = energy;
    }
  }
}

}  // namespace kaldi
