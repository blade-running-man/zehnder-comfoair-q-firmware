# Pure, and depends on nothing -- not even comfoair_secrets. That is what lets test/test_json put
# a hostile body against it on the host, which is the only place the cases it exists for can be
# written at all: a lone surrogate, an escaped NUL, a document that ends in the middle of a
# string. Every one of them is a body somebody can send to the open setup access point.
#
# NOT listed, deliberately: esp_http_server, and anything that could reach it. This component is
# handed a buffer and returns what it decoded; the moment it can send a response it can answer a
# request, and then the access policy in comfoair_policy has a second door. Same rule, and same
# wording, as components/comfoair_captive and components/comfoair_auth.
idf_component_register(
    SRCS "comfoair_json.cpp"
    INCLUDE_DIRS "include"
)
