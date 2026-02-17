build_lib_example(
  NAME iacr-test
  SOURCE_FILES iacr-test.cc
  LIBRARIES_TO_LINK
    ${libwifi}
    ${libinternet}
    ${libiacr_2}
    ${libinternet-apps}
    ${libmobility}
)

build_lib_example(
  NAME iacr-example
  SOURCE_FILES iacr-example.cc
  LIBRARIES_TO_LINK
    ${libwifi}
    ${libinternet}
    ${libiacr_2}
    ${libinternet-apps}
    ${libmobility}
)
