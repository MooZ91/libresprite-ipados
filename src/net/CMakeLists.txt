# Aseprite Network Library
# Copyright (C) 2001-2015 David Capello

# http_request.cpp and http_request_null.cpp are mutually exclusive: each is
# guarded by __has_include(<curl/curl.h>), so exactly one of them compiles to
# anything depending on whether libcurl is available on the target.
add_library(net-lib
  http_request.cpp
  http_request_null.cpp
  http_response.cpp)

if(CURL_FOUND)
  target_link_libraries(net-lib ${CURL_LIBRARIES})
endif()
