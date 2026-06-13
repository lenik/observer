# bash completion for oremind

_oremind()
{
	local cur prev words cword
	_init_completion || return

	case "$prev" in
		-t|--theme)
			COMPREPLY=($(compgen -W 'dark light' -- "$cur"))
			return
			;;
		-i|--interval)
			COMPREPLY=()
			return
			;;
		-d|--sqlite-db)
			_filedir
			return
			;;
	esac

	if [[ $cur == -* ]]; then
		COMPREPLY=($(compgen -W '-v --verbose -q --quiet -h --help -t --theme -i --interval -d --sqlite-db --version' -- "$cur"))
		return
	fi

	COMPREPLY=()
}

complete -F _oremind oremind
