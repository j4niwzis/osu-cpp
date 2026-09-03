(list
 (channel
  (name 'guix)
  ;; A history-preserving mirror of the official channel. The GitHub mirror
  ;; is an orphan snapshot, so time-machine correctly rejects its commits as
  ;; unrelated to Guix's authenticated channel introduction.
  (url "https://codeberg.org/guix/guix.git")
  ;; Pinned master: it contains the repository's Skia package, while the
  ;; bootstrap Guix 1.5 package set predates it.
  (commit "0802546301e0a9fab4d43b872ddac96c753a2430")
  (introduction
   (make-channel-introduction
    "9edb3f66fd807b096b48283debdcddccfea34bad"
    (openpgp-fingerprint
     "BBB0 2DDF 2CEA F6A8 0D1D E643 A2A0 6DF2 A33A 54FA")))))
