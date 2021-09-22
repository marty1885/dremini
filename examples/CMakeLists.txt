add_executable(gemini_client gemini_client/gemini_client.cpp)
target_link_libraries(gemini_client PRIVATE dremini)

add_executable(basic_gemlog basic_gemlog/main.cpp)
target_link_libraries(basic_gemlog PRIVATE dremini)
add_custom_command(
  TARGET basic_gemlog POST_BUILD
  COMMAND ${CMAKE_COMMAND}
          -E
          copy_directory
          ${CMAKE_CURRENT_SOURCE_DIR}/basic_gemlog/files
          $<TARGET_FILE_DIR:basic_gemlog>)
add_custom_command(
  TARGET basic_gemlog POST_BUILD
  COMMAND ${CMAKE_COMMAND}
          -E
	  copy_if_different
          ${CMAKE_CURRENT_SOURCE_DIR}/basic_gemlog/drogon.config.json
          $<TARGET_FILE_DIR:basic_gemlog>)

drogon_create_views(basic_gemlog ${CMAKE_CURRENT_SOURCE_DIR}/basic_gemlog/templates/
          ${CMAKE_CURRENT_BINARY_DIR})
