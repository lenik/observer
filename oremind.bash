# bash completion for oremind

_oremind()
{
	local cur prev words cword
	_init_completion || return

	case "$prev" in
		-l|--locale)
			COMPREPLY=($(compgen -W 'en zh_CN zh_TW ja ko th vi fr de it eo' -- "$cur"))
			return
			;;
		-t|--theme)
			COMPREPLY=($(compgen -W 'dark light' -- "$cur"))
			return
			;;
		-o|--opacity)
			COMPREPLY=()
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
		COMPREPLY=($(compgen -W '-v --verbose -q --quiet -h --help -l --locale -t --theme -o --opacity -i --interval -d --sqlite-db --version' -- "$cur"))
		return
	fi

	COMPREPLY=()
}

complete -F _oremind oremind
