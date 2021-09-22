add_executable(test_server test_server/test_server.cpp)
target_link_libraries(test_server PRIVATE dremini)

add_custom_command(
  TARGET test_server POST_BUILD
  COMMAND ${CMAKE_COMMAND}
          -E
          copy_if_different
          ${CMAKE_CURRENT_SOURCE_DIR}/test_server/index.gmi
          ${CMAKE_CURRENT_SOURCE_DIR}/test_server/about.gmi
          ${CMAKE_CURRENT_SOURCE_DIR}/test_server/key.pem
          ${CMAKE_CURRENT_SOURCE_DIR}/test_server/cert.pem
          ${CMAKE_CURRENT_SOURCE_DIR}/test_server/drogon.config.json
          $<TARGET_FILE_DIR:test_server>)


add_executable(test_client test_client/test_client.cpp)
target_link_libraries(test_client PRIVATE dremini)
ParseAndAddDrogonTests(test_client)
