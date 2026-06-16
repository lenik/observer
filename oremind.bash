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
		-c|--cancel)
			COMPREPLY=()
			return
			;;
		-i|--interval)
			COMPREPLY=()
			return
			;;
		-w|--weekstart)
			COMPREPLY=($(compgen -W 'M m S s' -- "$cur"))
			return
			;;
		-d|--database)
			_filedir
			return
			;;
	esac

	if [[ $cur == -* ]]; then
		COMPREPLY=($(compgen -W '-v --verbose -q --quiet -h --help -l --locale -t --theme -o --opacity -c --cancel -i --interval -w --weekstart -d --database --version' -- "$cur"))
		return
	fi

	COMPREPLY=()
}

complete -F _oremind oremind
