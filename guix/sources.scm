;; Written by tools/lock-to-guix.py from standalone/cme-lock.json.
;; Every one of these is a fact the lock is already holding the build to.
;;
;; A list of (port version origin): the name is what the port is
;; called, so a package can name the input after it and write the
;; declaration that says where its sources landed.
(list
  (list "basu" "0.2.1"
    (origin
      (method git-fetch)
      (uri (git-reference (url "https://git.sr.ht/~emersion/basu")
                          (commit "684a41d68cfbb05e38aacb60a8548e21ddfbecdb")))
      (file-name "basu-0.2.1-checkout")
      (sha256 (base32 "054mg6f9aqi0i3i3w8fc37qnns1vng3qq5b8nfd9g51wi8h891nc"))))
  (list "boost-algorithm" "1.92.0"
    (origin
      (method git-fetch)
      (uri (git-reference (url "https://github.com/boostorg/algorithm.git")
                          (commit "fd1cf19c0f84c483b3310c34fd600fe8b2725ccc")))
      (file-name "boost-algorithm-1.92.0-checkout")
      (sha256 (base32 "14287gwdvj1ls5yiz61a4bz4b630q5fsbfph93ca39lm2yw0k878"))))
  (list "boost-align" "1.92.0"
    (origin
      (method git-fetch)
      (uri (git-reference (url "https://github.com/boostorg/align.git")
                          (commit "440281d63d1c0b7c7fde63ded67b4860b57d5756")))
      (file-name "boost-align-1.92.0-checkout")
      (sha256 (base32 "0c31lra2q16xhp33sgma1h9kabnzfd7rbgfb326swh33fk1jdyh1"))))
  (list "boost-array" "1.92.0"
    (origin
      (method git-fetch)
      (uri (git-reference (url "https://github.com/boostorg/array.git")
                          (commit "3df3aafd1924084d46988590bd94cf4c1b362859")))
      (file-name "boost-array-1.92.0-checkout")
      (sha256 (base32 "11pfvy81423k6ljg3lbsvi67h7s1mkl3lnlh7awvqfdydnzi0icj"))))
  (list "boost-asio-core" "1.92.0"
    (origin
      (method git-fetch)
      (uri (git-reference (url "https://github.com/boostorg/asio.git")
                          (commit "4fa4abee89a62fdeeccac2585caece625f40647e")))
      (file-name "boost-asio-core-1.92.0-checkout")
      (sha256 (base32 "1pf6sqd8p21f73732lxdpmf6822br5b8f56krlhmwrm9cy0yn5x4"))))
  (list "boost-assert" "1.92.0"
    (origin
      (method git-fetch)
      (uri (git-reference (url "https://github.com/boostorg/assert.git")
                          (commit "fc2a476cc7d9f42b65ec104e90d24bfd6290efdc")))
      (file-name "boost-assert-1.92.0-checkout")
      (sha256 (base32 "0phglbgl79l4g1iyaklxqj714081hpjwfw54s2dmss8zg0aphxdq"))))
  (list "boost-beast" "1.92.0"
    (origin
      (method git-fetch)
      (uri (git-reference (url "https://github.com/boostorg/beast.git")
                          (commit "7c1e061f91e2ef542217b76286c314d006c0c8fc")))
      (file-name "boost-beast-1.92.0-checkout")
      (sha256 (base32 "1hil82r7rj9invsvl7rd0vyj5b6wiqjkshb89mdf9d327z4hdair"))))
  (list "boost-bind" "1.92.0"
    (origin
      (method git-fetch)
      (uri (git-reference (url "https://github.com/boostorg/bind.git")
                          (commit "8cc29fc19db49e791743c821821e201b46ab9c66")))
      (file-name "boost-bind-1.92.0-checkout")
      (sha256 (base32 "0x90wbd2wsxvlxygiiq0zq187y4b5yfblllnq698nijrvx3ls0ml"))))
  (list "boost-compat" "1.92.0"
    (origin
      (method git-fetch)
      (uri (git-reference (url "https://github.com/boostorg/compat.git")
                          (commit "349fb928b5ef800d1b8544cb4c382c39ecde0b3c")))
      (file-name "boost-compat-1.92.0-checkout")
      (sha256 (base32 "01f1b1fzqid6cdrxr50px194dwnyk9fi6gbw9slrh91xfd1k5x5f"))))
  (list "boost-concept-check" "1.92.0"
    (origin
      (method git-fetch)
      (uri (git-reference (url "https://github.com/boostorg/concept_check.git")
                          (commit "235e54ebf23be678045e0eeae90f47ca0e2c95ce")))
      (file-name "boost-concept-check-1.92.0-checkout")
      (sha256 (base32 "0dk2sw96jmdi5rxykb6bn8xhrpx94p5ws77vj2ikvmky6wsn8w65"))))
  (list "boost-config" "1.92.0"
    (origin
      (method git-fetch)
      (uri (git-reference (url "https://github.com/boostorg/config.git")
                          (commit "115e718e0fd72329e69fc776dac99811385d6f77")))
      (file-name "boost-config-1.92.0-checkout")
      (sha256 (base32 "1w73irnigffqap5l9cs8gx8bl6ybzsb26wpzag9xwlm0i5ig2cia"))))
  (list "boost-container" "1.92.0"
    (origin
      (method git-fetch)
      (uri (git-reference (url "https://github.com/boostorg/container.git")
                          (commit "f74270de43714edccf17b33eca454348d47402b7")))
      (file-name "boost-container-1.92.0-checkout")
      (sha256 (base32 "0a3xihbpymglfxcc9sq1chj0igajkkkjy9xgqj24z8v87kpljmad"))))
  (list "boost-container-hash" "1.92.0"
    (origin
      (method git-fetch)
      (uri (git-reference (url "https://github.com/boostorg/container_hash.git")
                          (commit "2698b43803c012601e6bb1a6116e83767b97986c")))
      (file-name "boost-container-hash-1.92.0-checkout")
      (sha256 (base32 "0sq91nqb5gczkjfz5zjv8n6x4qqxfmcs2mpf5q8ih51r19mv225x"))))
  (list "boost-conversion" "1.92.0"
    (origin
      (method git-fetch)
      (uri (git-reference (url "https://github.com/boostorg/conversion.git")
                          (commit "71b14ad1dae1d2be91ad310007c749ca93dc2e72")))
      (file-name "boost-conversion-1.92.0-checkout")
      (sha256 (base32 "0dng53946k1cwq5v86d3228db3m4gr5j10z4r0ca0ziln90y34zr"))))
  (list "boost-core" "1.92.0"
    (origin
      (method git-fetch)
      (uri (git-reference (url "https://github.com/boostorg/core.git")
                          (commit "a90a31934fe8bcb6e6be6dfea77b80492c7b6c81")))
      (file-name "boost-core-1.92.0-checkout")
      (sha256 (base32 "13si4arspygsyq1mwshmnaink2pc26kj11vbw4bb5s28pcilvp33"))))
  (list "boost-describe" "1.92.0"
    (origin
      (method git-fetch)
      (uri (git-reference (url "https://github.com/boostorg/describe.git")
                          (commit "5e7b4b84c8d105093687b940a45ac22df47b1ab4")))
      (file-name "boost-describe-1.92.0-checkout")
      (sha256 (base32 "0zffnzml1gdvyz0cmag26brhvbalh71sq5m52d7a9dai61431r8q"))))
  (list "boost-detail" "1.92.0"
    (origin
      (method git-fetch)
      (uri (git-reference (url "https://github.com/boostorg/detail.git")
                          (commit "965826dc374165d71530d9814ecd5f4628365522")))
      (file-name "boost-detail-1.92.0-checkout")
      (sha256 (base32 "10wymqq4aa13qk8hxvjwwq2q5qvzhnqyw8alvlspjpnnwgcbqwwc"))))
  (list "boost-endian" "1.92.0"
    (origin
      (method git-fetch)
      (uri (git-reference (url "https://github.com/boostorg/endian.git")
                          (commit "4bffdf3defc2836409e72622066a40d8396088ae")))
      (file-name "boost-endian-1.92.0-checkout")
      (sha256 (base32 "0gy4pp5zj7ma5nzc2jrv4w21dbq7garxrbn02ccmy1gddyrqgjrx"))))
  (list "boost-exception" "1.92.0"
    (origin
      (method git-fetch)
      (uri (git-reference (url "https://github.com/boostorg/exception.git")
                          (commit "afcb28d8f2517eda7b6b2cba1cd8b6dc3bbaf0d9")))
      (file-name "boost-exception-1.92.0-checkout")
      (sha256 (base32 "1q1d7qd8pk3d0l0llh99mzdzdhpvbq4g5cfbmhp2q1s8myzp86vn"))))
  (list "boost-function" "1.92.0"
    (origin
      (method git-fetch)
      (uri (git-reference (url "https://github.com/boostorg/function.git")
                          (commit "18650af5175ea247aebc60ff12db1b477123d5dc")))
      (file-name "boost-function-1.92.0-checkout")
      (sha256 (base32 "0c4dd45xhs52apsc78wlxilcyy617qrc01944zawbbria8mdmfr0"))))
  (list "boost-function-types" "1.92.0"
    (origin
      (method git-fetch)
      (uri (git-reference (url "https://github.com/boostorg/function_types.git")
                          (commit "e454e797fbd2e1df704306e8ef70836e8bcb71ae")))
      (file-name "boost-function-types-1.92.0-checkout")
      (sha256 (base32 "0djl5ar8bdh6f02g9npp8yhmp3f4w5xaf4ygfgfgbwrc460gdcm3"))))
  (list "boost-fusion" "1.92.0"
    (origin
      (method git-fetch)
      (uri (git-reference (url "https://github.com/boostorg/fusion.git")
                          (commit "017a8399fd62e81cf11ecea8c4063d055088458e")))
      (file-name "boost-fusion-1.92.0-checkout")
      (sha256 (base32 "047fai3dvgpgv3azsmzjgmyb7z25jyn3fcmppzrg21ja62skdm4d"))))
  (list "boost-headers" "1.92.0"
    (origin
      (method git-fetch)
      (uri (git-reference (url "https://github.com/boostorg/headers.git")
                          (commit "95930ca8f5d144fe345a2ad7a2a7728b8c3e5cd5")))
      (file-name "boost-headers-1.92.0-checkout")
      (sha256 (base32 "148maw0a6shbvw2niiwwr90i9gsbvxzv5dcwy3rjvvlkd93q8ppp"))))
  (list "boost-integer" "1.92.0"
    (origin
      (method git-fetch)
      (uri (git-reference (url "https://github.com/boostorg/integer.git")
                          (commit "e513075061f125c631a420e8960e0c606bd4b810")))
      (file-name "boost-integer-1.92.0-checkout")
      (sha256 (base32 "0grm5slqv2750kk1zlwaw02b1bzwd8hq3d7z38ykg5cz5cp4fb0g"))))
  (list "boost-intrusive" "1.92.0"
    (origin
      (method git-fetch)
      (uri (git-reference (url "https://github.com/boostorg/intrusive.git")
                          (commit "b089da5af88981d6e87392680b0b68fd30be0b12")))
      (file-name "boost-intrusive-1.92.0-checkout")
      (sha256 (base32 "09xzvmh48y2br1n6h3rkf7f0xnh2g7m743grfx4zi7mh1khx29cc"))))
  (list "boost-io" "1.92.0"
    (origin
      (method git-fetch)
      (uri (git-reference (url "https://github.com/boostorg/io.git")
                          (commit "342e4c6d10d586058818daa84201a2d301357a53")))
      (file-name "boost-io-1.92.0-checkout")
      (sha256 (base32 "1kqjfjbqfn79wvbsdk0f2093lvgdgz97wm22m9rjhvzjfh4amvgc"))))
  (list "boost-iterator" "1.92.0"
    (origin
      (method git-fetch)
      (uri (git-reference (url "https://github.com/boostorg/iterator.git")
                          (commit "031662886357f9172b448604d11127f629efbc0b")))
      (file-name "boost-iterator-1.92.0-checkout")
      (sha256 (base32 "0z7wr3849qffwww60hqpdjqkggir9h7j96s3hcn7hj2wk432ckgw"))))
  (list "boost-json" "1.92.0"
    (origin
      (method git-fetch)
      (uri (git-reference (url "https://github.com/boostorg/json.git")
                          (commit "c57359dff379f278d7d5f8fa332d3dea684ba5fa")))
      (file-name "boost-json-1.92.0-checkout")
      (sha256 (base32 "1nxs0ilys2fkyhvnnwxda83l19cz9bs8w77c0gi0i9a646rgp137"))))
  (list "boost-lexical-cast" "1.92.0"
    (origin
      (method git-fetch)
      (uri (git-reference (url "https://github.com/boostorg/lexical_cast.git")
                          (commit "35d8af6ce21fa7a163b68a2fb27437d3fb737232")))
      (file-name "boost-lexical-cast-1.92.0-checkout")
      (sha256 (base32 "12n8jzkk3xhj82zpsr0dx4hpgkdlbj3c989hz7wlqdjwxw69yipf"))))
  (list "boost-logic" "1.92.0"
    (origin
      (method git-fetch)
      (uri (git-reference (url "https://github.com/boostorg/logic.git")
                          (commit "9b8703a2d6623405323b892f1d126a6b17ca1651")))
      (file-name "boost-logic-1.92.0-checkout")
      (sha256 (base32 "03w2bq6yzp3b7kvb8x3wg6gbv6s8qnlgc3c85wg7sqny6hihfjhr"))))
  (list "boost-move" "1.92.0"
    (origin
      (method git-fetch)
      (uri (git-reference (url "https://github.com/boostorg/move.git")
                          (commit "b1ecb39a75ed2de17c23c46a005ac79f59a528ca")))
      (file-name "boost-move-1.92.0-checkout")
      (sha256 (base32 "0qvzrs4lfj0pz6vmvjymzx5dr04sw46s2rix3ixn6fr3927qvjfd"))))
  (list "boost-mp11" "1.92.0"
    (origin
      (method git-fetch)
      (uri (git-reference (url "https://github.com/boostorg/mp11.git")
                          (commit "c3eba6ac6be2e21af33a1b2ec97634cd01bcc447")))
      (file-name "boost-mp11-1.92.0-checkout")
      (sha256 (base32 "0343pwg5s4r580nhr75pqxp8h1y8jnc99rvly89blhzlmhfia5xj"))))
  (list "boost-mpl" "1.92.0"
    (origin
      (method git-fetch)
      (uri (git-reference (url "https://github.com/boostorg/mpl.git")
                          (commit "9d1f81ffeb055ea1ce0d96370bf07c40a2843878")))
      (file-name "boost-mpl-1.92.0-checkout")
      (sha256 (base32 "0m267rv5k1nw9qv3a35iflnzwqvwnacv3v3m5qqg987fsl94sl4g"))))
  (list "boost-numeric-conversion" "1.92.0"
    (origin
      (method git-fetch)
      (uri (git-reference (url "https://github.com/boostorg/numeric_conversion.git")
                          (commit "d1b479f7a4aa54d8ffb93d8dc4ee0c24670210d8")))
      (file-name "boost-numeric-conversion-1.92.0-checkout")
      (sha256 (base32 "1s5dmy0z4pkb06xm4k61irw7vm18l67wyvf0azicfalbpixzpnxv"))))
  (list "boost-optional" "1.92.0"
    (origin
      (method git-fetch)
      (uri (git-reference (url "https://github.com/boostorg/optional.git")
                          (commit "c0648f5f2d7b7d8e59c6e2c1d203f34c7aec129e")))
      (file-name "boost-optional-1.92.0-checkout")
      (sha256 (base32 "0madpxgzdsps5vqbz4irhiskabpr73m0f5684zxzzjwdwc8zcij2"))))
  (list "boost-pool" "1.92.0"
    (origin
      (method git-fetch)
      (uri (git-reference (url "https://github.com/boostorg/pool.git")
                          (commit "740c8076f9d02f0216e8f3dbb15d2fd80f67d7f4")))
      (file-name "boost-pool-1.92.0-checkout")
      (sha256 (base32 "1wrpbf85xjynszs2jds8w0m0gsqavlhwmrsiw57pvkn8c65cbsi6"))))
  (list "boost-predef" "1.92.0"
    (origin
      (method git-fetch)
      (uri (git-reference (url "https://github.com/boostorg/predef.git")
                          (commit "e1211a4ca467bb6512e99025772ca25afa8d6159")))
      (file-name "boost-predef-1.92.0-checkout")
      (sha256 (base32 "1mr6zpmwkhkx0blaanikb6knb64ibwws3z4b0j9clb34aj9qqmhb"))))
  (list "boost-preprocessor" "1.92.0"
    (origin
      (method git-fetch)
      (uri (git-reference (url "https://github.com/boostorg/preprocessor.git")
                          (commit "cd1b1bd03900b68505822cfa25cb16851bd6caf1")))
      (file-name "boost-preprocessor-1.92.0-checkout")
      (sha256 (base32 "1w458fcy2yp6dbgdl8w6f1sdamgi0zsfaqxa9asivbvw1rbchr70"))))
  (list "boost-range" "1.92.0"
    (origin
      (method git-fetch)
      (uri (git-reference (url "https://github.com/boostorg/range.git")
                          (commit "7481e429b023655a6e77799d5f2cf4788145d494")))
      (file-name "boost-range-1.92.0-checkout")
      (sha256 (base32 "0znm44zn6dx56c4v4x0c0wn5qfvrh8sln0j2k8g5vq2q2dwbhpal"))))
  (list "boost-regex" "1.92.0"
    (origin
      (method git-fetch)
      (uri (git-reference (url "https://github.com/boostorg/regex.git")
                          (commit "7760ef2a61d643c1770026f0392b440bdfb5687d")))
      (file-name "boost-regex-1.92.0-checkout")
      (sha256 (base32 "1ydzsj0ljpwp8pmdrp7qgdifk17gpgxd2af5y3yl36nblvgi3vi6"))))
  (list "boost-smart-ptr" "1.92.0"
    (origin
      (method git-fetch)
      (uri (git-reference (url "https://github.com/boostorg/smart_ptr.git")
                          (commit "6e945160d788b8efdfc49ba4af1f8797cacd7c97")))
      (file-name "boost-smart-ptr-1.92.0-checkout")
      (sha256 (base32 "00663g0821qypgk1k1m2b54ga7wq1w1s2s3mvi5spskx5j8gr6ds"))))
  (list "boost-static-string" "1.92.0"
    (origin
      (method git-fetch)
      (uri (git-reference (url "https://github.com/boostorg/static_string.git")
                          (commit "2ff9d3535e853f4913e85e7fea28f84b04ea5d81")))
      (file-name "boost-static-string-1.92.0-checkout")
      (sha256 (base32 "0y3crgvgr1ry2v1kni63zjnw1y9hsvvfqa91xwwafhvmrwxh6r7m"))))
  (list "boost-system" "1.92.0"
    (origin
      (method git-fetch)
      (uri (git-reference (url "https://github.com/boostorg/system.git")
                          (commit "bc7c00fa67501ceadfde8e920835502340e8b899")))
      (file-name "boost-system-1.92.0-checkout")
      (sha256 (base32 "10kjs4zddl1mg9wajicwl5x0kb9hf4fafr63810qr59naraazlc0"))))
  (list "boost-throw-exception" "1.92.0"
    (origin
      (method git-fetch)
      (uri (git-reference (url "https://github.com/boostorg/throw_exception.git")
                          (commit "0924b53b40d1da33301f94fb97518f5a7df31e9b")))
      (file-name "boost-throw-exception-1.92.0-checkout")
      (sha256 (base32 "10dmddx5qgmxzy6xxp3z4y09r4wvh9db384hjarb4nrms5w2c5ld"))))
  (list "boost-tokenizer" "1.92.0"
    (origin
      (method git-fetch)
      (uri (git-reference (url "https://github.com/boostorg/tokenizer.git")
                          (commit "743082f58e964e7cef353a9678edaae055691ca0")))
      (file-name "boost-tokenizer-1.92.0-checkout")
      (sha256 (base32 "0500mv3via7jvz971qygcw94vj3v39hkna1kpr7hz34bys3lwg5c"))))
  (list "boost-tuple" "1.92.0"
    (origin
      (method git-fetch)
      (uri (git-reference (url "https://github.com/boostorg/tuple.git")
                          (commit "704830d883825357a83e49a2aed2a07e734a74bb")))
      (file-name "boost-tuple-1.92.0-checkout")
      (sha256 (base32 "1f2mm2kp2wgszlfqa4kvifjgj0xwhxhwqjk9lan751xyxmxb2d48"))))
  (list "boost-type-index" "1.92.0"
    (origin
      (method git-fetch)
      (uri (git-reference (url "https://github.com/boostorg/type_index.git")
                          (commit "af648a10037497055d6f0823e22cf7e394e38458")))
      (file-name "boost-type-index-1.92.0-checkout")
      (sha256 (base32 "0jzfchv6iz4j3l4dfxygrqf2ya8y0lnr5cm2k6sb3bkrz2106vpa"))))
  (list "boost-type-traits" "1.92.0"
    (origin
      (method git-fetch)
      (uri (git-reference (url "https://github.com/boostorg/type_traits.git")
                          (commit "e6275ccf01c9cf8775ef0cb6188bd58f3b167a0f")))
      (file-name "boost-type-traits-1.92.0-checkout")
      (sha256 (base32 "121bhwqi807rm14n6rv6mr4h4in3dw65s012rfnd7p0bbpw7dmvz"))))
  (list "boost-typeof" "1.92.0"
    (origin
      (method git-fetch)
      (uri (git-reference (url "https://github.com/boostorg/typeof.git")
                          (commit "06748a1d65182a2dacfb9e0aa5dfc6230353b66d")))
      (file-name "boost-typeof-1.92.0-checkout")
      (sha256 (base32 "0zrk7kslcd9zx32pif2n2k62gp348zzp2iw99f7b4qpam8ndprhm"))))
  (list "boost-unordered" "1.92.0"
    (origin
      (method git-fetch)
      (uri (git-reference (url "https://github.com/boostorg/unordered.git")
                          (commit "636164f1357cf217374313820a457c31b50fcfc7")))
      (file-name "boost-unordered-1.92.0-checkout")
      (sha256 (base32 "0hlfi2b8f5322my9q3i2wzlzp68wz1z8yna4a4pyl3jrgmzs9a9k"))))
  (list "boost-utility" "1.92.0"
    (origin
      (method git-fetch)
      (uri (git-reference (url "https://github.com/boostorg/utility.git")
                          (commit "8679ac0f1f769fa8d705a1d2329afb5fb6a1eaf2")))
      (file-name "boost-utility-1.92.0-checkout")
      (sha256 (base32 "1dgvj9q8vcfwkpl720l3zspcc2wgmbdx0hgbplm5czda9zhvw71i"))))
  (list "boost-variant2" "1.92.0"
    (origin
      (method git-fetch)
      (uri (git-reference (url "https://github.com/boostorg/variant2.git")
                          (commit "dde1a3ac91d6986bae27b7f740689804d56cff61")))
      (file-name "boost-variant2-1.92.0-checkout")
      (sha256 (base32 "13agvjin627lvamhmdzl4jgnlxz0bjyn47nhbm0140mmgll05vfz"))))
  (list "boost-winapi" "1.92.0"
    (origin
      (method git-fetch)
      (uri (git-reference (url "https://github.com/boostorg/winapi.git")
                          (commit "094612ee33a8dce960cd075537266fdb4788d059")))
      (file-name "boost-winapi-1.92.0-checkout")
      (sha256 (base32 "1cijwvrjxiwsk7szyw6vwdkyjfpb69wphsx2si5bxlyrqz23jnx7"))))
  (list "expat" "2.6.4"
    (origin
      (method git-fetch)
      (uri (git-reference (url "https://github.com/libexpat/libexpat.git")
                          (commit "2691aff4304a6d7e053199c205620136481b9dd1")))
      (file-name "expat-2.6.4-checkout")
      (sha256 (base32 "0bwm4ds6fivhdpvvrdw0a6hxwzmhil0gj37jxnrnya0vrzfkykvs"))))
  (list "flac" "1.4.3"
    (origin
      (method git-fetch)
      (uri (git-reference (url "https://github.com/xiph/flac.git")
                          (commit "28e4f0528c76b296c561e922ba67d43751990599")))
      (file-name "flac-1.4.3-checkout")
      (sha256 (base32 "09d6pj9fl693lnl51zwgkizlkk4k179vpx0bx13mpv93z5kpkp6d"))))
  (list "fmt" "12.2.0"
    (origin
      (method git-fetch)
      (uri (git-reference (url "https://github.com/fmtlib/fmt.git")
                          (commit "1be298e1bd68957e4cd352e1f676f00e07dcfb57")))
      (file-name "fmt-12.2.0-checkout")
      (sha256 (base32 "0nkb975pmky40z8wx2ksz7yw7nhjhkqir6m1qfivdgslvjcczkjd"))))
  (list "freetype" "2.13.3"
    (origin
      (method git-fetch)
      (uri (git-reference (url "https://gitlab.freedesktop.org/freetype/freetype.git")
                          (commit "42608f77f20749dd6ddc9e0536788eaad70ea4b5")))
      (file-name "freetype-2.13.3-checkout")
      (sha256 (base32 "0xzprk58jcs08q5kaifkf3pvgp58xckyx9hxjrqnx0k97fa78pz2"))))
  (list "glfw" "3.4"
    (origin
      (method git-fetch)
      (uri (git-reference (url "https://github.com/glfw/glfw.git")
                          (commit "7b6aead9fb88b3623e3b3725ebb42670cbe4c579")))
      (file-name "glfw-3.4-checkout")
      (sha256 (base32 "1izxbb55hzi0b6jnfi11nvfsd3l85xzvb66jsb0ipkfxs95mdiqy"))))
  (list "libjpeg-turbo" "3.0.4"
    (origin
      (method git-fetch)
      (uri (git-reference (url "https://github.com/libjpeg-turbo/libjpeg-turbo.git")
                          (commit "f29eda648547b36aa594c4116c7764a6c8a079b9")))
      (file-name "libjpeg-turbo-3.0.4-checkout")
      (sha256 (base32 "05yywd4855721c8c8954qbdq7k1gfvawdqgw59bk7jq524nmrqz5"))))
  (list "libpng" "1.6.44"
    (origin
      (method git-fetch)
      (uri (git-reference (url "https://github.com/pnggroup/libpng.git")
                          (commit "f5e92d76973a7a53f517579bc95d61483bf108c0")))
      (file-name "libpng-1.6.44-checkout")
      (sha256 (base32 "0ffnp3q9kmiqz3lprbxb1p5v689zffk39s35hmk8fmdacs0cf5w0"))))
  (list "libsndfile" "1.2.2"
    (origin
      (method git-fetch)
      (uri (git-reference (url "https://github.com/libsndfile/libsndfile.git")
                          (commit "72f6af15e8f85157bd622ed45b979025828b7001")))
      (file-name "libsndfile-1.2.2-checkout")
      (sha256 (base32 "1n3qq18kqwwhgd7blihvfhafrl4ni0n455zfv1n6a5f0mwsic5iz"))))
  (list "libzip" "1.11.2"
    (origin
      (method git-fetch)
      (uri (git-reference (url "https://github.com/nih-at/libzip.git")
                          (commit "64b62d6b1a686a1b0bac1b6b9dcb635be0499afb")))
      (file-name "libzip-1.11.2-checkout")
      (sha256 (base32 "1vf4w1zvkmn2dss2sf95p87w8zhamnhfj69jpq5kn2ywmj6jg38d"))))
  (list "mpg123" "1.32.10"
    (origin
      (method url-fetch)
      (uri "https://www.mpg123.de/download/mpg123-1.32.10.tar.bz2")
      (file-name "mpg123-1.32.10.tar.bz2")
      (hash (content-hash "0gqrzjm17m1f07qj0x0xismdvg9a4vys4c267m54dghjgss0bfxxlp449jmpbr7snw0fzya8qsc2l01ag9z7bzgiggv2hp6mxnfgwsd" sha512))))
  (list "ogg" "1.3.5"
    (origin
      (method git-fetch)
      (uri (git-reference (url "https://github.com/xiph/ogg.git")
                          (commit "e1774cd77f471443541596e09078e78fdc342e4f")))
      (file-name "ogg-1.3.5-checkout")
      (sha256 (base32 "1jxnh8skfw7xqq7qsrwbxivja6ryw59daf7q8yskfdiskyg3cxya"))))
  (list "openal-soft" "1.25.2"
    (origin
      (method git-fetch)
      (uri (git-reference (url "https://github.com/kcat/openal-soft.git")
                          (commit "b2c48f7718ef3fcf67921a8b6534c4914e328970")))
      (file-name "openal-soft-1.25.2-checkout")
      (sha256 (base32 "0v1cs3rkgmb3rmfz60ilhga5kdmh9prnhj31ha8wx0vc3slbj8gv"))))
  (list "opus" "1.5.2"
    (origin
      (method git-fetch)
      (uri (git-reference (url "https://github.com/xiph/opus.git")
                          (commit "ddbe48383984d56acd9e1ab6a090c54ca6b735a6")))
      (file-name "opus-1.5.2-checkout")
      (sha256 (base32 "0bwxc0wgyffwglfq6l0179xrd4q7w8gi02jym6i3nw58k9yngyc9"))))
  (list "skia" "153"
    (origin
      (method url-fetch)
      (uri "https://codeload.github.com/google/skia/tar.gz/9d07e5bad9e3e21da2426946e589daa647218271")
      (file-name "skia-153")
      (hash (content-hash "0cdsviyig15jb8kn93w16pbdnj1pg3p49y560xk0k4ay09i5p2zbj6j5h4nafnr4rxzg38zyhrcsj0xdayn27qkn6gd87b79qcq45lw" sha512))))
  (list "skiff" ""
    (origin
      (method git-fetch)
      (uri (git-reference (url "https://github.com/j4niwzis/skiff.git")
                          (commit "4884907b6209aec96a03d5b1164c1374035e9082")))
      (file-name "skiff--checkout")
      (sha256 (base32 "0wf4djzh7ix6bkqvriz7a65j3fsc6fd9lqnysnh60mbhvi92qd3v"))))
  (list "skiff-widgets" ""
    (origin
      (method git-fetch)
      (uri (git-reference (url "https://github.com/j4niwzis/skiff-widgets.git")
                          (commit "065ff975ef99fe197b9b2b5d2762c060e91c5af2")))
      (file-name "skiff-widgets--checkout")
      (sha256 (base32 "087pyycb5m4jqnyk8084hgwwi6lkh8jvx5ykgxxipjcjkpcrssba"))))
  (list "vorbis" "1.3.7"
    (origin
      (method git-fetch)
      (uri (git-reference (url "https://github.com/xiph/vorbis.git")
                          (commit "0657aee69dec8508a0011f47f3b69d7538e9d262")))
      (file-name "vorbis-1.3.7-checkout")
      (sha256 (base32 "0ml91vxfjhad7di457jqvfyjssjfhggmaisyw2xxwr0v341xnaf8"))))
  (list "vulkan-headers" "1.4.303"
    (origin
      (method git-fetch)
      (uri (git-reference (url "https://github.com/KhronosGroup/Vulkan-Headers.git")
                          (commit "6a74a7d65cafa19e38ec116651436cce6efd5b2e")))
      (file-name "vulkan-headers-1.4.303-checkout")
      (sha256 (base32 "0vl1aq0cb4f1naxjgc3vk3kch6f6hg4wsvwq2pifyydr0sxwf6sv"))))
  (list "xz" "5.6.3"
    (origin
      (method git-fetch)
      (uri (git-reference (url "https://github.com/tukaani-project/xz.git")
                          (commit "9331ce4009ddc839f5191d234cc41b2d4797376d")))
      (file-name "xz-5.6.3-checkout")
      (sha256 (base32 "0iipcnk8nb2yldkgcfdj6wsf1crb4z97qmm8x6jkdp7mwbvhzb42"))))
  (list "zlib" "1.3.1"
    (origin
      (method git-fetch)
      (uri (git-reference (url "https://github.com/madler/zlib.git")
                          (commit "51b7f2abdade71cd9bb0e7a373ef2610ec6f9daf")))
      (file-name "zlib-1.3.1-checkout")
      (sha256 (base32 "12r98gay98w6vd1m6v1xw6k3yzc4yb7r3x6h5zjw6hbr4dcwnhsf"))))
  )
