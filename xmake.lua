set_project("spud")
set_languages("c++20")

add_rules("mode.debug")
add_rules("mode.release")
add_rules("mode.releasedbg")

add_requires("zydis")
add_requires("asmjit")

target("spud")
do
    set_kind("static")
    add_files("src/*.cc")
    add_headerfiles("src/*.h")

    add_includedirs("include", { public = true })

    add_packages("zydis")
    add_packages("asmjit")
end
