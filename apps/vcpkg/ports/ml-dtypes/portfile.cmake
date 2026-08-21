set(VCPKG_POLICY_EMPTY_INCLUDE_FOLDER enabled)

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO jax-ml/ml_dtypes
    REF 9fd1a480f1cdb23b3d28dfea5eadf3d84b6dfc62  # v0.5.4
    SHA512 b04a5968e1c6fa143d018e927888597feb97b52d2df214538f517e74a5cc87a412ccc4dff4e1a65d40f048e08d13c74778c1c584f1ad12f4ae92fffb5dd6321d
)

file(INSTALL "${SOURCE_PATH}/" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}/src")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/share/${PORT}/src/third_party")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
