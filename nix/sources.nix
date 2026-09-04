# Written by tools/lock-to-nix.py from standalone/cme-lock.json.
# Every one of these is a fact the lock is already holding the build to.
{ pkgs ? import <nixpkgs> { } }:
{
  basu = pkgs.fetchgit {
    url = "https://git.sr.ht/~emersion/basu";
    rev = "684a41d68cfbb05e38aacb60a8548e21ddfbecdb";
    sha256 = "054mg6f9aqi0i3i3w8fc37qnns1vng3qq5b8nfd9g51wi8h891nc";
    fetchSubmodules = false;
  };  # 0.2.1
  boost_algorithm = pkgs.fetchgit {
    url = "https://github.com/boostorg/algorithm.git";
    rev = "fd1cf19c0f84c483b3310c34fd600fe8b2725ccc";
    sha256 = "14287gwdvj1ls5yiz61a4bz4b630q5fsbfph93ca39lm2yw0k878";
    fetchSubmodules = false;
  };  # 1.92.0
  boost_align = pkgs.fetchgit {
    url = "https://github.com/boostorg/align.git";
    rev = "440281d63d1c0b7c7fde63ded67b4860b57d5756";
    sha256 = "0c31lra2q16xhp33sgma1h9kabnzfd7rbgfb326swh33fk1jdyh1";
    fetchSubmodules = false;
  };  # 1.92.0
  boost_array = pkgs.fetchgit {
    url = "https://github.com/boostorg/array.git";
    rev = "3df3aafd1924084d46988590bd94cf4c1b362859";
    sha256 = "11pfvy81423k6ljg3lbsvi67h7s1mkl3lnlh7awvqfdydnzi0icj";
    fetchSubmodules = false;
  };  # 1.92.0
  boost_asio_core = pkgs.fetchgit {
    url = "https://github.com/boostorg/asio.git";
    rev = "4fa4abee89a62fdeeccac2585caece625f40647e";
    sha256 = "1pf6sqd8p21f73732lxdpmf6822br5b8f56krlhmwrm9cy0yn5x4";
    fetchSubmodules = false;
  };  # 1.92.0
  boost_assert = pkgs.fetchgit {
    url = "https://github.com/boostorg/assert.git";
    rev = "fc2a476cc7d9f42b65ec104e90d24bfd6290efdc";
    sha256 = "0phglbgl79l4g1iyaklxqj714081hpjwfw54s2dmss8zg0aphxdq";
    fetchSubmodules = false;
  };  # 1.92.0
  boost_beast = pkgs.fetchgit {
    url = "https://github.com/boostorg/beast.git";
    rev = "7c1e061f91e2ef542217b76286c314d006c0c8fc";
    sha256 = "1hil82r7rj9invsvl7rd0vyj5b6wiqjkshb89mdf9d327z4hdair";
    fetchSubmodules = false;
  };  # 1.92.0
  boost_bind = pkgs.fetchgit {
    url = "https://github.com/boostorg/bind.git";
    rev = "8cc29fc19db49e791743c821821e201b46ab9c66";
    sha256 = "0x90wbd2wsxvlxygiiq0zq187y4b5yfblllnq698nijrvx3ls0ml";
    fetchSubmodules = false;
  };  # 1.92.0
  boost_compat = pkgs.fetchgit {
    url = "https://github.com/boostorg/compat.git";
    rev = "349fb928b5ef800d1b8544cb4c382c39ecde0b3c";
    sha256 = "01f1b1fzqid6cdrxr50px194dwnyk9fi6gbw9slrh91xfd1k5x5f";
    fetchSubmodules = false;
  };  # 1.92.0
  boost_concept_check = pkgs.fetchgit {
    url = "https://github.com/boostorg/concept_check.git";
    rev = "235e54ebf23be678045e0eeae90f47ca0e2c95ce";
    sha256 = "0dk2sw96jmdi5rxykb6bn8xhrpx94p5ws77vj2ikvmky6wsn8w65";
    fetchSubmodules = false;
  };  # 1.92.0
  boost_config = pkgs.fetchgit {
    url = "https://github.com/boostorg/config.git";
    rev = "115e718e0fd72329e69fc776dac99811385d6f77";
    sha256 = "1w73irnigffqap5l9cs8gx8bl6ybzsb26wpzag9xwlm0i5ig2cia";
    fetchSubmodules = false;
  };  # 1.92.0
  boost_container = pkgs.fetchgit {
    url = "https://github.com/boostorg/container.git";
    rev = "f74270de43714edccf17b33eca454348d47402b7";
    sha256 = "0a3xihbpymglfxcc9sq1chj0igajkkkjy9xgqj24z8v87kpljmad";
    fetchSubmodules = false;
  };  # 1.92.0
  boost_container_hash = pkgs.fetchgit {
    url = "https://github.com/boostorg/container_hash.git";
    rev = "2698b43803c012601e6bb1a6116e83767b97986c";
    sha256 = "0sq91nqb5gczkjfz5zjv8n6x4qqxfmcs2mpf5q8ih51r19mv225x";
    fetchSubmodules = false;
  };  # 1.92.0
  boost_conversion = pkgs.fetchgit {
    url = "https://github.com/boostorg/conversion.git";
    rev = "71b14ad1dae1d2be91ad310007c749ca93dc2e72";
    sha256 = "0dng53946k1cwq5v86d3228db3m4gr5j10z4r0ca0ziln90y34zr";
    fetchSubmodules = false;
  };  # 1.92.0
  boost_core = pkgs.fetchgit {
    url = "https://github.com/boostorg/core.git";
    rev = "a90a31934fe8bcb6e6be6dfea77b80492c7b6c81";
    sha256 = "13si4arspygsyq1mwshmnaink2pc26kj11vbw4bb5s28pcilvp33";
    fetchSubmodules = false;
  };  # 1.92.0
  boost_describe = pkgs.fetchgit {
    url = "https://github.com/boostorg/describe.git";
    rev = "5e7b4b84c8d105093687b940a45ac22df47b1ab4";
    sha256 = "0zffnzml1gdvyz0cmag26brhvbalh71sq5m52d7a9dai61431r8q";
    fetchSubmodules = false;
  };  # 1.92.0
  boost_detail = pkgs.fetchgit {
    url = "https://github.com/boostorg/detail.git";
    rev = "965826dc374165d71530d9814ecd5f4628365522";
    sha256 = "10wymqq4aa13qk8hxvjwwq2q5qvzhnqyw8alvlspjpnnwgcbqwwc";
    fetchSubmodules = false;
  };  # 1.92.0
  boost_endian = pkgs.fetchgit {
    url = "https://github.com/boostorg/endian.git";
    rev = "4bffdf3defc2836409e72622066a40d8396088ae";
    sha256 = "0gy4pp5zj7ma5nzc2jrv4w21dbq7garxrbn02ccmy1gddyrqgjrx";
    fetchSubmodules = false;
  };  # 1.92.0
  boost_exception = pkgs.fetchgit {
    url = "https://github.com/boostorg/exception.git";
    rev = "afcb28d8f2517eda7b6b2cba1cd8b6dc3bbaf0d9";
    sha256 = "1q1d7qd8pk3d0l0llh99mzdzdhpvbq4g5cfbmhp2q1s8myzp86vn";
    fetchSubmodules = false;
  };  # 1.92.0
  boost_function = pkgs.fetchgit {
    url = "https://github.com/boostorg/function.git";
    rev = "18650af5175ea247aebc60ff12db1b477123d5dc";
    sha256 = "0c4dd45xhs52apsc78wlxilcyy617qrc01944zawbbria8mdmfr0";
    fetchSubmodules = false;
  };  # 1.92.0
  boost_function_types = pkgs.fetchgit {
    url = "https://github.com/boostorg/function_types.git";
    rev = "e454e797fbd2e1df704306e8ef70836e8bcb71ae";
    sha256 = "0djl5ar8bdh6f02g9npp8yhmp3f4w5xaf4ygfgfgbwrc460gdcm3";
    fetchSubmodules = false;
  };  # 1.92.0
  boost_fusion = pkgs.fetchgit {
    url = "https://github.com/boostorg/fusion.git";
    rev = "017a8399fd62e81cf11ecea8c4063d055088458e";
    sha256 = "047fai3dvgpgv3azsmzjgmyb7z25jyn3fcmppzrg21ja62skdm4d";
    fetchSubmodules = false;
  };  # 1.92.0
  boost_headers = pkgs.fetchgit {
    url = "https://github.com/boostorg/headers.git";
    rev = "95930ca8f5d144fe345a2ad7a2a7728b8c3e5cd5";
    sha256 = "148maw0a6shbvw2niiwwr90i9gsbvxzv5dcwy3rjvvlkd93q8ppp";
    fetchSubmodules = false;
  };  # 1.92.0
  boost_integer = pkgs.fetchgit {
    url = "https://github.com/boostorg/integer.git";
    rev = "e513075061f125c631a420e8960e0c606bd4b810";
    sha256 = "0grm5slqv2750kk1zlwaw02b1bzwd8hq3d7z38ykg5cz5cp4fb0g";
    fetchSubmodules = false;
  };  # 1.92.0
  boost_intrusive = pkgs.fetchgit {
    url = "https://github.com/boostorg/intrusive.git";
    rev = "b089da5af88981d6e87392680b0b68fd30be0b12";
    sha256 = "09xzvmh48y2br1n6h3rkf7f0xnh2g7m743grfx4zi7mh1khx29cc";
    fetchSubmodules = false;
  };  # 1.92.0
  boost_io = pkgs.fetchgit {
    url = "https://github.com/boostorg/io.git";
    rev = "342e4c6d10d586058818daa84201a2d301357a53";
    sha256 = "1kqjfjbqfn79wvbsdk0f2093lvgdgz97wm22m9rjhvzjfh4amvgc";
    fetchSubmodules = false;
  };  # 1.92.0
  boost_iterator = pkgs.fetchgit {
    url = "https://github.com/boostorg/iterator.git";
    rev = "031662886357f9172b448604d11127f629efbc0b";
    sha256 = "0z7wr3849qffwww60hqpdjqkggir9h7j96s3hcn7hj2wk432ckgw";
    fetchSubmodules = false;
  };  # 1.92.0
  boost_json = pkgs.fetchgit {
    url = "https://github.com/boostorg/json.git";
    rev = "c57359dff379f278d7d5f8fa332d3dea684ba5fa";
    sha256 = "1nxs0ilys2fkyhvnnwxda83l19cz9bs8w77c0gi0i9a646rgp137";
    fetchSubmodules = false;
  };  # 1.92.0
  boost_lexical_cast = pkgs.fetchgit {
    url = "https://github.com/boostorg/lexical_cast.git";
    rev = "35d8af6ce21fa7a163b68a2fb27437d3fb737232";
    sha256 = "12n8jzkk3xhj82zpsr0dx4hpgkdlbj3c989hz7wlqdjwxw69yipf";
    fetchSubmodules = false;
  };  # 1.92.0
  boost_logic = pkgs.fetchgit {
    url = "https://github.com/boostorg/logic.git";
    rev = "9b8703a2d6623405323b892f1d126a6b17ca1651";
    sha256 = "03w2bq6yzp3b7kvb8x3wg6gbv6s8qnlgc3c85wg7sqny6hihfjhr";
    fetchSubmodules = false;
  };  # 1.92.0
  boost_move = pkgs.fetchgit {
    url = "https://github.com/boostorg/move.git";
    rev = "b1ecb39a75ed2de17c23c46a005ac79f59a528ca";
    sha256 = "0qvzrs4lfj0pz6vmvjymzx5dr04sw46s2rix3ixn6fr3927qvjfd";
    fetchSubmodules = false;
  };  # 1.92.0
  boost_mp11 = pkgs.fetchgit {
    url = "https://github.com/boostorg/mp11.git";
    rev = "c3eba6ac6be2e21af33a1b2ec97634cd01bcc447";
    sha256 = "0343pwg5s4r580nhr75pqxp8h1y8jnc99rvly89blhzlmhfia5xj";
    fetchSubmodules = false;
  };  # 1.92.0
  boost_mpl = pkgs.fetchgit {
    url = "https://github.com/boostorg/mpl.git";
    rev = "9d1f81ffeb055ea1ce0d96370bf07c40a2843878";
    sha256 = "0m267rv5k1nw9qv3a35iflnzwqvwnacv3v3m5qqg987fsl94sl4g";
    fetchSubmodules = false;
  };  # 1.92.0
  boost_numeric_conversion = pkgs.fetchgit {
    url = "https://github.com/boostorg/numeric_conversion.git";
    rev = "d1b479f7a4aa54d8ffb93d8dc4ee0c24670210d8";
    sha256 = "1s5dmy0z4pkb06xm4k61irw7vm18l67wyvf0azicfalbpixzpnxv";
    fetchSubmodules = false;
  };  # 1.92.0
  boost_optional = pkgs.fetchgit {
    url = "https://github.com/boostorg/optional.git";
    rev = "c0648f5f2d7b7d8e59c6e2c1d203f34c7aec129e";
    sha256 = "0madpxgzdsps5vqbz4irhiskabpr73m0f5684zxzzjwdwc8zcij2";
    fetchSubmodules = false;
  };  # 1.92.0
  boost_pool = pkgs.fetchgit {
    url = "https://github.com/boostorg/pool.git";
    rev = "740c8076f9d02f0216e8f3dbb15d2fd80f67d7f4";
    sha256 = "1wrpbf85xjynszs2jds8w0m0gsqavlhwmrsiw57pvkn8c65cbsi6";
    fetchSubmodules = false;
  };  # 1.92.0
  boost_predef = pkgs.fetchgit {
    url = "https://github.com/boostorg/predef.git";
    rev = "e1211a4ca467bb6512e99025772ca25afa8d6159";
    sha256 = "1mr6zpmwkhkx0blaanikb6knb64ibwws3z4b0j9clb34aj9qqmhb";
    fetchSubmodules = false;
  };  # 1.92.0
  boost_preprocessor = pkgs.fetchgit {
    url = "https://github.com/boostorg/preprocessor.git";
    rev = "cd1b1bd03900b68505822cfa25cb16851bd6caf1";
    sha256 = "1w458fcy2yp6dbgdl8w6f1sdamgi0zsfaqxa9asivbvw1rbchr70";
    fetchSubmodules = false;
  };  # 1.92.0
  boost_range = pkgs.fetchgit {
    url = "https://github.com/boostorg/range.git";
    rev = "7481e429b023655a6e77799d5f2cf4788145d494";
    sha256 = "0znm44zn6dx56c4v4x0c0wn5qfvrh8sln0j2k8g5vq2q2dwbhpal";
    fetchSubmodules = false;
  };  # 1.92.0
  boost_regex = pkgs.fetchgit {
    url = "https://github.com/boostorg/regex.git";
    rev = "7760ef2a61d643c1770026f0392b440bdfb5687d";
    sha256 = "1ydzsj0ljpwp8pmdrp7qgdifk17gpgxd2af5y3yl36nblvgi3vi6";
    fetchSubmodules = false;
  };  # 1.92.0
  boost_smart_ptr = pkgs.fetchgit {
    url = "https://github.com/boostorg/smart_ptr.git";
    rev = "6e945160d788b8efdfc49ba4af1f8797cacd7c97";
    sha256 = "00663g0821qypgk1k1m2b54ga7wq1w1s2s3mvi5spskx5j8gr6ds";
    fetchSubmodules = false;
  };  # 1.92.0
  boost_static_string = pkgs.fetchgit {
    url = "https://github.com/boostorg/static_string.git";
    rev = "2ff9d3535e853f4913e85e7fea28f84b04ea5d81";
    sha256 = "0y3crgvgr1ry2v1kni63zjnw1y9hsvvfqa91xwwafhvmrwxh6r7m";
    fetchSubmodules = false;
  };  # 1.92.0
  boost_system = pkgs.fetchgit {
    url = "https://github.com/boostorg/system.git";
    rev = "bc7c00fa67501ceadfde8e920835502340e8b899";
    sha256 = "10kjs4zddl1mg9wajicwl5x0kb9hf4fafr63810qr59naraazlc0";
    fetchSubmodules = false;
  };  # 1.92.0
  boost_throw_exception = pkgs.fetchgit {
    url = "https://github.com/boostorg/throw_exception.git";
    rev = "0924b53b40d1da33301f94fb97518f5a7df31e9b";
    sha256 = "10dmddx5qgmxzy6xxp3z4y09r4wvh9db384hjarb4nrms5w2c5ld";
    fetchSubmodules = false;
  };  # 1.92.0
  boost_tokenizer = pkgs.fetchgit {
    url = "https://github.com/boostorg/tokenizer.git";
    rev = "743082f58e964e7cef353a9678edaae055691ca0";
    sha256 = "0500mv3via7jvz971qygcw94vj3v39hkna1kpr7hz34bys3lwg5c";
    fetchSubmodules = false;
  };  # 1.92.0
  boost_tuple = pkgs.fetchgit {
    url = "https://github.com/boostorg/tuple.git";
    rev = "704830d883825357a83e49a2aed2a07e734a74bb";
    sha256 = "1f2mm2kp2wgszlfqa4kvifjgj0xwhxhwqjk9lan751xyxmxb2d48";
    fetchSubmodules = false;
  };  # 1.92.0
  boost_type_index = pkgs.fetchgit {
    url = "https://github.com/boostorg/type_index.git";
    rev = "af648a10037497055d6f0823e22cf7e394e38458";
    sha256 = "0jzfchv6iz4j3l4dfxygrqf2ya8y0lnr5cm2k6sb3bkrz2106vpa";
    fetchSubmodules = false;
  };  # 1.92.0
  boost_type_traits = pkgs.fetchgit {
    url = "https://github.com/boostorg/type_traits.git";
    rev = "e6275ccf01c9cf8775ef0cb6188bd58f3b167a0f";
    sha256 = "121bhwqi807rm14n6rv6mr4h4in3dw65s012rfnd7p0bbpw7dmvz";
    fetchSubmodules = false;
  };  # 1.92.0
  boost_typeof = pkgs.fetchgit {
    url = "https://github.com/boostorg/typeof.git";
    rev = "06748a1d65182a2dacfb9e0aa5dfc6230353b66d";
    sha256 = "0zrk7kslcd9zx32pif2n2k62gp348zzp2iw99f7b4qpam8ndprhm";
    fetchSubmodules = false;
  };  # 1.92.0
  boost_unordered = pkgs.fetchgit {
    url = "https://github.com/boostorg/unordered.git";
    rev = "636164f1357cf217374313820a457c31b50fcfc7";
    sha256 = "0hlfi2b8f5322my9q3i2wzlzp68wz1z8yna4a4pyl3jrgmzs9a9k";
    fetchSubmodules = false;
  };  # 1.92.0
  boost_utility = pkgs.fetchgit {
    url = "https://github.com/boostorg/utility.git";
    rev = "8679ac0f1f769fa8d705a1d2329afb5fb6a1eaf2";
    sha256 = "1dgvj9q8vcfwkpl720l3zspcc2wgmbdx0hgbplm5czda9zhvw71i";
    fetchSubmodules = false;
  };  # 1.92.0
  boost_variant2 = pkgs.fetchgit {
    url = "https://github.com/boostorg/variant2.git";
    rev = "dde1a3ac91d6986bae27b7f740689804d56cff61";
    sha256 = "13agvjin627lvamhmdzl4jgnlxz0bjyn47nhbm0140mmgll05vfz";
    fetchSubmodules = false;
  };  # 1.92.0
  boost_winapi = pkgs.fetchgit {
    url = "https://github.com/boostorg/winapi.git";
    rev = "094612ee33a8dce960cd075537266fdb4788d059";
    sha256 = "1cijwvrjxiwsk7szyw6vwdkyjfpb69wphsx2si5bxlyrqz23jnx7";
    fetchSubmodules = false;
  };  # 1.92.0
  expat = pkgs.fetchgit {
    url = "https://github.com/libexpat/libexpat.git";
    rev = "2691aff4304a6d7e053199c205620136481b9dd1";
    sha256 = "0bwm4ds6fivhdpvvrdw0a6hxwzmhil0gj37jxnrnya0vrzfkykvs";
    fetchSubmodules = false;
  };  # 2.6.4
  flac = pkgs.fetchgit {
    url = "https://github.com/xiph/flac.git";
    rev = "28e4f0528c76b296c561e922ba67d43751990599";
    sha256 = "09d6pj9fl693lnl51zwgkizlkk4k179vpx0bx13mpv93z5kpkp6d";
    fetchSubmodules = false;
  };  # 1.4.3
  fmt = pkgs.fetchgit {
    url = "https://github.com/fmtlib/fmt.git";
    rev = "1be298e1bd68957e4cd352e1f676f00e07dcfb57";
    sha256 = "0nkb975pmky40z8wx2ksz7yw7nhjhkqir6m1qfivdgslvjcczkjd";
    fetchSubmodules = false;
  };  # 12.2.0
  freetype = pkgs.fetchgit {
    url = "https://gitlab.freedesktop.org/freetype/freetype.git";
    rev = "42608f77f20749dd6ddc9e0536788eaad70ea4b5";
    sha256 = "0xzprk58jcs08q5kaifkf3pvgp58xckyx9hxjrqnx0k97fa78pz2";
    fetchSubmodules = false;
  };  # 2.13.3
  glfw = pkgs.fetchgit {
    url = "https://github.com/glfw/glfw.git";
    rev = "7b6aead9fb88b3623e3b3725ebb42670cbe4c579";
    sha256 = "1izxbb55hzi0b6jnfi11nvfsd3l85xzvb66jsb0ipkfxs95mdiqy";
    fetchSubmodules = false;
  };  # 3.4
  libjpeg_turbo = pkgs.fetchgit {
    url = "https://github.com/libjpeg-turbo/libjpeg-turbo.git";
    rev = "f29eda648547b36aa594c4116c7764a6c8a079b9";
    sha256 = "05yywd4855721c8c8954qbdq7k1gfvawdqgw59bk7jq524nmrqz5";
    fetchSubmodules = false;
  };  # 3.0.4
  libpng = pkgs.fetchgit {
    url = "https://github.com/pnggroup/libpng.git";
    rev = "f5e92d76973a7a53f517579bc95d61483bf108c0";
    sha256 = "0ffnp3q9kmiqz3lprbxb1p5v689zffk39s35hmk8fmdacs0cf5w0";
    fetchSubmodules = false;
  };  # 1.6.44
  libsndfile = pkgs.fetchgit {
    url = "https://github.com/libsndfile/libsndfile.git";
    rev = "72f6af15e8f85157bd622ed45b979025828b7001";
    sha256 = "1n3qq18kqwwhgd7blihvfhafrl4ni0n455zfv1n6a5f0mwsic5iz";
    fetchSubmodules = false;
  };  # 1.2.2
  libzip = pkgs.fetchgit {
    url = "https://github.com/nih-at/libzip.git";
    rev = "64b62d6b1a686a1b0bac1b6b9dcb635be0499afb";
    sha256 = "1vf4w1zvkmn2dss2sf95p87w8zhamnhfj69jpq5kn2ywmj6jg38d";
    fetchSubmodules = false;
  };  # 1.11.2
  mpg123 = pkgs.fetchurl {
    url = "https://www.mpg123.de/download/mpg123-1.32.10.tar.bz2";
    hash = "sha512-TfPnbK/mQrHfi++v89NTAVDBNEbKfwe41SevW2Ui5NLe3QJaPwlfI6UeIxjRfhOV3m5Vxw46kPgAF+oJVf6MHw==";
  };  # 1.32.10
  ogg = pkgs.fetchgit {
    url = "https://github.com/xiph/ogg.git";
    rev = "e1774cd77f471443541596e09078e78fdc342e4f";
    sha256 = "1jxnh8skfw7xqq7qsrwbxivja6ryw59daf7q8yskfdiskyg3cxya";
    fetchSubmodules = false;
  };  # 1.3.5
  openal_soft = pkgs.fetchgit {
    url = "https://github.com/kcat/openal-soft.git";
    rev = "b2c48f7718ef3fcf67921a8b6534c4914e328970";
    sha256 = "0v1cs3rkgmb3rmfz60ilhga5kdmh9prnhj31ha8wx0vc3slbj8gv";
    fetchSubmodules = false;
  };  # 1.25.2
  opus = pkgs.fetchgit {
    url = "https://github.com/xiph/opus.git";
    rev = "ddbe48383984d56acd9e1ab6a090c54ca6b735a6";
    sha256 = "0bwxc0wgyffwglfq6l0179xrd4q7w8gi02jym6i3nw58k9yngyc9";
    fetchSubmodules = false;
  };  # 1.5.2
  skia = pkgs.fetchurl {
    url = "https://codeload.github.com/google/skia/tar.gz/9d07e5bad9e3e21da2426946e589daa647218271";
    hash = "sha512-nBaCGU5nHdSesRMfYb1qHUjNMvQfjfd7Jtk6ZQks0sj1xS0xAa/IBLMDU3wid7wbpG3rmsBHshMtWcKLPm7dGA==";
  };  # 153
  skiff = pkgs.fetchgit {
    url = "https://github.com/j4niwzis/skiff.git";
    rev = "da0760f4559b5aa02d5da9b366d4eb610c01688b";
    sha256 = "1s38mac9vcsiwnz7adbh0fha3cii0fryyjm44xsa1y84n4nf4hnl";
    fetchSubmodules = false;
  };  # 
  skiff_widgets = pkgs.fetchgit {
    url = "https://github.com/j4niwzis/skiff-widgets.git";
    rev = "065ff975ef99fe197b9b2b5d2762c060e91c5af2";
    sha256 = "087pyycb5m4jqnyk8084hgwwi6lkh8jvx5ykgxxipjcjkpcrssba";
    fetchSubmodules = false;
  };  # 
  vorbis = pkgs.fetchgit {
    url = "https://github.com/xiph/vorbis.git";
    rev = "0657aee69dec8508a0011f47f3b69d7538e9d262";
    sha256 = "0ml91vxfjhad7di457jqvfyjssjfhggmaisyw2xxwr0v341xnaf8";
    fetchSubmodules = false;
  };  # 1.3.7
  vulkan_headers = pkgs.fetchgit {
    url = "https://github.com/KhronosGroup/Vulkan-Headers.git";
    rev = "6a74a7d65cafa19e38ec116651436cce6efd5b2e";
    sha256 = "0vl1aq0cb4f1naxjgc3vk3kch6f6hg4wsvwq2pifyydr0sxwf6sv";
    fetchSubmodules = false;
  };  # 1.4.303
  xz = pkgs.fetchgit {
    url = "https://github.com/tukaani-project/xz.git";
    rev = "9331ce4009ddc839f5191d234cc41b2d4797376d";
    sha256 = "0iipcnk8nb2yldkgcfdj6wsf1crb4z97qmm8x6jkdp7mwbvhzb42";
    fetchSubmodules = false;
  };  # 5.6.3
  zlib = pkgs.fetchgit {
    url = "https://github.com/madler/zlib.git";
    rev = "51b7f2abdade71cd9bb0e7a373ef2610ec6f9daf";
    sha256 = "12r98gay98w6vd1m6v1xw6k3yzc4yb7r3x6h5zjw6hbr4dcwnhsf";
    fetchSubmodules = false;
  };  # 1.3.1
}
