# Here-documents: expanded and literal bodies.
x=world
cat <<EOF
hello $x
EOF

cat <<'EOF'
literal $x
EOF

cat <<-EOF
	tabs are stripped
	EOF
