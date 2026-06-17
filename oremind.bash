# bash completion for oremind

_oremind_locales='en zh_CN zh_TW ja ko th vi fr de it eo'
_oremind_themes='dark light innocent maiden girl morandi github ios msdos windows'
_oremind_opts='-v --verbose -q --quiet -h --help -l --locale -t --theme -o --opacity -c --cancel -i --interval -w --weekstart -d --database --diag --version'

_oremind()
{
	local cur prev words cword
	_init_completion || return

	case "$prev" in
		-l|--locale)
			COMPREPLY=($(compgen -W "$_oremind_locales" -- "$cur"))
			return
			;;
		-t|--theme)
			COMPREPLY=($(compgen -W "$_oremind_themes" -- "$cur"))
			return
			;;
		-o|--opacity|-c|--cancel|-i|--interval)
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
		--diag|--version|-h|--help|-v|--verbose|-q|--quiet)
			COMPREPLY=()
			return
			;;
	esac

	if [[ "$cur" == *=* ]]; then
		local key="${cur%%=*}" val="${cur#*=}"
		case "$key" in
			--locale|-l)
				mapfile -t COMPREPLY < <(compgen -W "$_oremind_locales" -- "$val")
				;;
			--theme|-t)
				mapfile -t COMPREPLY < <(compgen -W "$_oremind_themes" -- "$val")
				;;
			*)
				COMPREPLY=()
				;;
		esac
		if ((${#COMPREPLY[@]})); then
			local reply
			reply=()
			for item in "${COMPREPLY[@]}"; do
				reply+=("${key}=${item}")
			done
			COMPREPLY=("${reply[@]}")
		fi
		return
	fi

	if [[ $cur == -* ]]; then
		COMPREPLY=($(compgen -W "$_oremind_opts" -- "$cur"))
		return
	fi

	COMPREPLY=()
}

complete -F _oremind oremind
