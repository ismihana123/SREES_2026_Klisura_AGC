set(AGCPLUGIN_NAME agc)				#Naziv prvog projekta u solution-u

file(GLOB AGCPLUGIN_CPP_COMMON_SOURCES  ${CMAKE_CURRENT_LIST_DIR}/src/*.cpp)
file(GLOB AGCPLUGIN_CPP_COMMON_INCS  ${CMAKE_CURRENT_LIST_DIR}/src/*.h)
file(GLOB AGCPLUGIN_INC_GUI  ${NATID_SDK_INC}/gui/*.h)
file(GLOB AGCPLUGIN_INC_TD  ${NATID_SDK_INC}/td/*.h)
file(GLOB AGCPLUGIN_INC_CNT  ${NATID_SDK_INC}/cnt/*.h)
file(GLOB AGCPLUGIN_INC_MU  ${NATID_SDK_INC}/mu/*.h)
file(GLOB AGCPLUGIN_INC_MEM  ${NATID_SDK_INC}/mem/*.h)
file(GLOB AGCPLUGIN_INC_FO ${NATID_SDK_INC}/fo/*.h)
file(GLOB AGCPLUGIN_INC_SC ${NATID_SDK_INC}/sc/*.h)
file(GLOB AGCPLUGIN_INC_SYST ${NATID_SDK_INC}/syst/*.h)
file(GLOB AGCPLUGIN_INC_DENSE ${NATID_SDK_INC}/dense/*.h)
file(GLOB AGCPLUGIN_INC_SPARSE ${NATID_SDK_INC}/sparse/*.h)

# add shared library (plugin is a shared executatable binary file)
add_library(${AGCPLUGIN_NAME} SHARED ${AGCPLUGIN_CPP_COMMON_SOURCES} ${AGCPLUGIN_INC_GUI} ${AGCPLUGIN_CPP_COMMON_INCS} 
							${AGCPLUGIN_INC_TD} ${AGCPLUGIN_INC_SYST} 
							${AGCPLUGIN_INC_CNT} ${AGCPLUGIN_INC_MU} ${AGCPLUGIN_INC_MEM} ${AGCPLUGIN_INC_FO}
							${AGCPLUGIN_INC_SC} ${AGCPLUGIN_INC_DENSE} ${AGCPLUGIN_INC_SPARSE})

source_group("inc\\inc"        FILES ${AGCPLUGIN_CPP_COMMON_INCS})
source_group("inc\\gui"        FILES ${AGCPLUGIN_INC_GUI})
source_group("inc\\td"        FILES ${AGCPLUGIN_INC_TD})
source_group("inc\\cnt"        FILES ${AGCPLUGIN_INC_CNT})
source_group("inc\\dense"        FILES ${AGCPLUGIN_INC_DENSE})
source_group("inc\\mu"        FILES ${AGCPLUGIN_INC_MU})
source_group("inc\\mem"        FILES ${AGCPLUGIN_INC_MEM})
source_group("inc\\fo"        FILES ${AGCPLUGIN_INC_FO})
source_group("inc\\sc"        FILES ${AGCPLUGIN_INC_SC})
source_group("inc\\sparse"        FILES ${AGCPLUGIN_INC_SPARSE})
source_group("inc\\syst"        FILES ${AGCPLUGIN_INC_SYST})

source_group("src\\cpp"			FILES ${AGCPLUGIN_CPP_COMMON_SOURCES})

target_link_libraries(${AGCPLUGIN_NAME} debug ${MU_LIB_DEBUG} optimized ${MU_LIB_RELEASE} 
										debug ${MATRIX_LIB_DEBUG} optimized ${MATRIX_LIB_RELEASE}
									  debug ${NATGUI_LIB_DEBUG} optimized ${NATGUI_LIB_RELEASE})
									
target_compile_definitions(${AGCPLUGIN_NAME} PUBLIC PLUGIN_EXPORTS)

#setIDEPropertiesForLib(${AGCPLUGIN_NAME})



